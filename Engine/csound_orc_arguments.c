/*
  csound_orc_arguments.c:

  Copyright (C) 2023

  This file is part of Csound.

  The Csound Library is free software; you can redistribute it
  and/or modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  Csound is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with Csound; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
  02110-1301 USA
*/

#include "csoundCore.h"
#include "csound_orc.h"
#include "csound_orc_arguments.h"
#include "csound_orc_expressions.h"
#include "csound_orc_semantics.h"
#include "csound_orc_structs.h"

extern int pnum(char*);
void print_tree(CSOUND *, char *, TREE *);
void do_baktrace(CSOUND *csound, uint64_t files);
extern OENTRIES* find_opcode2(CSOUND*, char*);
extern int is_in_optional_arg(char*);
extern int is_in_var_arg(char*);
extern char* get_expression_opcode_type(CSOUND*, TREE*);
extern char* get_boolean_expression_opcode_type(CSOUND*, TREE*);

static CSOUND_ORC_ARGUMENT* new_csound_orc_argument(
    CSOUND*,
    CSOUND_ORC_ARGUMENTS*,
    TREE*,
    TYPE_TABLE*
);

static int identCounter = 100;

static char* generateUniqueIdent(CSOUND* csound) {
    char *ident = (char *)csound->Calloc(csound, 32);
    snprintf(ident, 32, "__internal__%d", identCounter);
    identCounter += 1;
    return ident;
}

static CSOUND_ORC_ARGUMENTS* get_arguments_from_tree(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENTS* args,
    TREE* tree,
    TYPE_TABLE* typeTable,
    int isAssignee
);


static void printOrcArgs(
    CSOUND_ORC_ARGUMENTS* args
) {
    if (args->length == 0) {
        printf("== ARGUMENT LIST EMPTY ==\n");
        return;
    }
    CONS_CELL* car = args->list;
    CSOUND_ORC_ARGUMENT* arg;
    int n = 0;
    while(car != NULL) {
        arg = (CSOUND_ORC_ARGUMENT*) car->value;
        printf("== ARGUMENT LIST INDEX %d ==\n", n);
        printf("\t text: %s \n", arg->text);
        printf("\t uid: %s \n", arg->uid);
        printf("\t type: %s \n", arg->cstype == NULL ? arg->text :
         arg->cstype->varTypeName);
        printf("\t isGlobal: %d \n", arg->isGlobal);
        printf("\t dimensions: %d \n", arg->dimensions);
        printf("\t isExpression: %d \n", arg->isExpression);
        car = car->next;
        n += 1;
    }
}

void printNode(TREE* tree) {
    printf("== BEG ==\n");
    if (tree == NULL) {
        return;
    }

    if (tree->value) {
        if (tree->value->lexeme != NULL) {
            printf("tree->value->lexeme: %s\n", tree->value->lexeme);
        } else {
            printf("missing: tree->value->lexeme\n");
        }
        if (tree->value->optype != NULL) {
            printf("tree->value->optype (annotation): %s\n", tree->value->optype);
        }

    } else {
        printf("missing: tree->value\n");
    }
    if (tree->type) {
        printf("tree->type: %d\n", tree->type);
    }
    if (tree->line) {
        printf("tree->line: %d\n", tree->line);
    }

    printf("== END ==\n");
}

// for pretty printing the given arguments
static char* make_comma_sep_arglist(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENTS* args,
    int displaySubExpr
) {
    char pretty[256];
    CONS_CELL* carSub;
    CONS_CELL* car = args->list;
    CSOUND_ORC_ARGUMENT* arg;
    CSOUND_ORC_ARGUMENT* argSub;
    char* p = pretty;
    int hasNext = car == NULL ? 0 : car->next != NULL;

    while (car != NULL) {
        if (car->value == NULL) {
            p += sprintf(p, "%s", "NULL");
            break;
        }
        arg = car->value;

        if (
            displaySubExpr &&
            arg->isExpression &&
            arg->SubExpression->length > 0
        ) {
            carSub = arg->SubExpression->list;
            argSub = carSub->value;
            int isArray = 0;
            int index = 0;
            int hasNextSub = carSub != NULL ? carSub->next != NULL : 0;

            while(carSub != NULL) {
                argSub = carSub->value;
                p += sprintf(
                    p,
                    hasNextSub ? "%s, " : "%s",
                    argSub->text
                );
                if (index == 0) {
                    isArray = argSub->dimensions > 0;
                    if (isArray) {
                        p += sprintf(p, "[");
                    }
                }
                carSub = carSub->next;
                hasNextSub = carSub != NULL ? carSub->next != NULL : 0;
                index += 1;
            }
            if (isArray) {
                p += sprintf(p, "]");
            }
            if (hasNext) {
                p += sprintf(p, ", ");
            }
        } else {
            p += sprintf(
                p,
                hasNext ? "%s, " : "%s",
                arg->text
            );
        }

        car = car->next;
        hasNext = car != NULL && car->next != NULL;

    }
    *p = '\0';
    return csound->Strdup(csound, pretty);
}

static void debugPrintArglist(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENTS* expr,
    CSOUND_ORC_ARGUMENT* parent,
    TREE* tree
) {
    if (tree->value == NULL && parent->isExpression) {
        printf("<expr %d> arglist: %s\n",
          tree->type,
          make_comma_sep_arglist(csound, expr, 0));
    } else if (tree->value == NULL) {
        printf("%s = opchar: %c arglist: %s\n",
          parent->text,
          tree->type,
          make_comma_sep_arglist(csound, expr, 0));
    } else {
        printf("%s = opname: %s arglist: %s\n",
          parent->text,
          tree->value->lexeme,
          make_comma_sep_arglist(csound, expr, 0));
    }

}


static int is_wildcard_type(char* typeIdent) {
  return strchr("?.*", *typeIdent) != NULL;
}

static int is_vararg_input_type(char* typeIdent) {
  return strlen(typeIdent) == 1 &&
    strchr("MNmWyzZ", *typeIdent) != NULL;
}

static int is_optarg_input_type(char* typeIdent) {
  return strchr("?IOoPpVvhJj", *typeIdent) != NULL;
}

static int is_init_opcode(char* opname) {
    return strncmp(opname, "init", 4) == 0;
}

static int is_internal_array_opcode(char* opname) {
    return strncmp(opname, "##array_", 8) == 0;
}

static int is_unary_token_type(int tokenType) {
  return (tokenType == S_UNOT) ||
    (tokenType == S_UMINUS) ||
    (tokenType == S_UPLUS);
}

static int is_legacy_t_rate_ident(char* ident) {
  char* token = ident;

  if (*token == '#') {
    token += 1;
  }
  if (*token == 'g') {
    token += 1;
  }
  return *token == 't';
}

static int fill_optional_inargs(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENTS* args,
    TYPE_TABLE* typeTable,
    OENTRY* candidate
) {
    char temp[6];
    char* current = candidate->intypes;
    CONS_CELL* car = args->list;

    // fast forward to the end
    while (car != NULL && *current != '\0') {
        if (*current == ':') {
            while (*current != '\0' && *current != ';') {
                current += 1;
            }
        }
        car = car->next;
        current++;
        while (*current == '[' || *current == ']') {
            current += 1;
        }
    }

    while (car == NULL && *current != '\0') {
        // end of explicit input
        // handle optional args
        CSOUND_ORC_ARGUMENT* optArg;
        memset(temp, '\0', 6);
        MYFLT optargValue = 0.0;

        switch(*current) {
            case 'O':
            case 'o': {
                optargValue = 0.0;
                break;
            }
            case 'P':
            case 'p': {
                optargValue = 1.0;
                break;
            }
            case 'q': {
                optargValue = 10.0;
                break;
            }
            case 'V':
            case 'v': {
                optargValue = 0.5;
                break;
            }
            case 'h': {
                optargValue = 127.0;
                break;
            }
            case 'J':
            case 'j': {
                optargValue = -1.0;
                break;
            }
            default: {
                // not a mismatch if it's a vararg at the end
                if (is_vararg_input_type(current)) {
                    return 1;
                } else {
                    return 0;
                }
            }
        }

        cs_sprintf(temp, "%f", optargValue);
        optArg = new_csound_orc_argument(
            csound,
            args,
            NULL,
            typeTable
        );

        optArg->text = csound->Strdup(csound, temp);
        optArg->cstype = (CS_TYPE*) &CS_VAR_TYPE_C;
        args->append(csound, args, optArg);
        current += 1;
    }

    return 1;
}

int check_satisfies_arate_input(
    char* typeIdent
) {
    return strchr("aXMNyx", *typeIdent) != NULL;
}

int check_satisfies_krate_input(
    char* typeIdent
) {
    return strchr("ckmxzXUOJVPN", *typeIdent) != NULL;
}

int check_satisfies_expected_input(
    CS_TYPE* cstype,
    char* typeIdent,
    int isArray
) {
    if (is_wildcard_type(typeIdent)) {
        return 1;
    }

    // printf("input: checking %c against %s\n", *typeIdent, arg->text);
    if (
        cstype == ((CS_TYPE*) &CS_VAR_TYPE_C) &&
        strchr("Sf", *typeIdent) == NULL // fewer letters to rule out than include
    ) {
        return 1;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_A)) {
        return check_satisfies_arate_input(typeIdent);
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_K)) {
        return check_satisfies_krate_input(typeIdent);
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_B)) {
        return strchr("B", *typeIdent) != NULL;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_b)) {
        return strchr("Bb", *typeIdent) != NULL;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_L)) {
        return strchr("l", *typeIdent) != NULL;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_S)) {
        return strchr("SNTW", *typeIdent) != NULL;
    } else if (
        cstype == ((CS_TYPE*) &CS_VAR_TYPE_I) ||
        cstype == ((CS_TYPE*) &CS_VAR_TYPE_R) ||
        cstype == ((CS_TYPE*) &CS_VAR_TYPE_P)
    ) {
        return isArray ?
            strchr("icoXUNcmI", *typeIdent) != NULL :
            strchr("ickoXUOJVPNTmIz", *typeIdent) != NULL;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_F)) {
        return strchr("f", *typeIdent) != NULL;
    }

    return 0;
}

int check_satisfies_alternating_z_input(
    char* typeIdent,
    int argIndex
) {
    if (argIndex % 2 == 0) {
        return check_satisfies_krate_input(typeIdent);
    } else {
        return check_satisfies_arate_input(typeIdent);
    }
}

static int check_satisfies_expected_output(
    CS_TYPE* cstype,
    char* typeIdent
) {
    if (is_wildcard_type(typeIdent)) {
        return 1;
    }

    // printf("input: checking %c against %s\n", *typeIdent, arg->text);
    if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_C)) {
        return 1;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_A)) {
        return strchr("amXN", *typeIdent) != NULL;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_K)) {
        return strchr("kzXN", *typeIdent) != NULL;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_B)) {
        return strchr("B", *typeIdent) != NULL;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_b)) {
        return strchr("Bb", *typeIdent) != NULL;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_L)) {
        return strchr("l", *typeIdent) != NULL;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_S)) {
        return strchr("SNI", *typeIdent) != NULL;
    } else if (
        cstype == ((CS_TYPE*) &CS_VAR_TYPE_I) ||
        cstype == ((CS_TYPE*) &CS_VAR_TYPE_R) ||
        cstype == ((CS_TYPE*) &CS_VAR_TYPE_P)
    ) {
        return strchr("zXNicmI", *typeIdent) != NULL;
    } else if (cstype == ((CS_TYPE*) &CS_VAR_TYPE_F)) {
        return strchr("f", *typeIdent) != NULL;
    }

    return 0;

}

// as with 'resolve_opcode_get_outarg', it returns the first match
// inline binary expression tree usually has left and right branches
// for input arguments, so both lists are treated as input arguments
// from left to right
OENTRY* resolve_opcode_with_orc_args(
    CSOUND* csound,
    OENTRIES* entries,
    CSOUND_ORC_ARGUMENTS* inlist,
    CSOUND_ORC_ARGUMENTS* outlist,
    TYPE_TABLE* typeTable,
    int skipOutargs // for subexpressions
) {
    for (int i = 0; i < entries->count; i++) {
        OENTRY* temp = entries->entries[i];
        if (strncmp(temp->opname, ":MyType", 5) == 0) {
            printf("yo\n");

        }
        int inArgsMatch = 0;
        int outArgsMatch = 0;
        int expectsInputs = temp->intypes != NULL && strlen(temp->intypes) != 0;
        int expectsOutputs = temp->outypes != NULL && strlen(temp->outypes) != 0;
        int alternatingInputListCount = 0; // needs to be even number

        if (inlist->length == 0 && !expectsInputs ||
            (expectsInputs && *temp->intypes == '*')) {
            inArgsMatch = 1;
        }

        // handle cases where inlist begins with optarg
        if (
            inlist->length == 0 &&
            expectsInputs &&
            is_optarg_input_type(temp->intypes)
        ) {
            inArgsMatch = 1;
        }

        if (
            skipOutargs || (outlist->length == 0 && !expectsOutputs) ||
            (expectsOutputs && *temp->outypes == '*')
        ) {
            outArgsMatch = 1;
        }

        if (!inArgsMatch && expectsInputs && inlist->length > 0) {
            char* current = temp->intypes;
            CONS_CELL* car = inlist->list;
            CSOUND_ORC_ARGUMENT* currentArg = car->value;
            int index = 0;
            while (currentArg != NULL && *current != '\0') {
                int isUserDefinedType = currentArg->cstype != NULL &&
                    currentArg->cstype->userDefinedType;
                int currentArgTextLength = 1;
                int dimensionsNeeded = currentArg->dimensions - currentArg->dimension;
                int dimensionsFound = 0;

                if (
                    dimensionsNeeded > 0 &&
                    is_internal_array_opcode(temp->opname)
                ) {
                    // ##array_get and ##array_set can work
                    // on any dimension and its
                    // intypes are non-specifc about it
                    dimensionsNeeded = 1;
                }
                if (isUserDefinedType) {
                    if (*current == '.') {
                        inArgsMatch = 1;
                    } else {
                        currentArgTextLength = strlen(
                            currentArg->cstype->varTypeName
                        );
                        inArgsMatch = strncmp(
                            current + 1,
                            currentArg->cstype->varTypeName,
                            currentArgTextLength
                        ) == 0;
                        currentArgTextLength += 2;
                    }
                } else if (*current == 'Z') {
                    inArgsMatch = check_satisfies_alternating_z_input(
                        currentArg->cstype->varTypeName,
                        alternatingInputListCount
                    );
                    alternatingInputListCount += 1;
                    if (!inArgsMatch) {
                        // at this point we dont compare if it's even
                        alternatingInputListCount = 0;
                        break;
                    }
                } else if (check_satisfies_expected_input(
                    currentArg->cstype,
                    current,
                    dimensionsNeeded > 0)
                ) {
                    inArgsMatch = 1;
                } else {
                    inArgsMatch = 0;
                    break;
                }

                if (
                    !isUserDefinedType &&
                    !is_vararg_input_type(current)
                ) {
                    current += currentArgTextLength;
                    while (*current == '[' || *current == ']') {
                        if (*current == '[') {
                            dimensionsFound += 1;
                        }
                        current += 1;
                    }

                    if (dimensionsNeeded != dimensionsFound) {
                        inArgsMatch = 0;
                        break;
                    }
                }
                car = car == NULL ? NULL : car->next;
                currentArg = car == NULL ? NULL : (CSOUND_ORC_ARGUMENT*) car->value;
                index += 1;
            }
        }

        if (
            alternatingInputListCount > 0 &&
            alternatingInputListCount % 2 != 0
        ) {
            inArgsMatch = 0;
        }

        if (inArgsMatch && !outArgsMatch && expectsOutputs && outlist->length > 0) {
            char* current = temp->outypes;
            CONS_CELL* car = outlist->list;
            CSOUND_ORC_ARGUMENT* currentArg = car->value;

            while (currentArg != NULL && *current != '\0' ) {
                int currentArgTextLength = 1;
                int isSyntheticVar = *currentArg->text == '#';
                int isUserDefinedType = currentArg->cstype != NULL ?
                    currentArg->cstype->userDefinedType :
                    0;
                int dimensionsNeeded = currentArg->dimensions;

                if (dimensionsNeeded > 0 && is_init_opcode(temp->opname)) {
                    // init family of opcodes intypes are non-specific
                    // about the level of dimensions
                    dimensionsNeeded = 1;
                }

                int dimensionsFound = 0;
                if (isUserDefinedType && *current == '.') {
                    outArgsMatch = 1;
                } else if (isUserDefinedType) {
                    // compare MyVar with :MyVar;
                    currentArgTextLength = strlen(
                        currentArg->cstype->varTypeName
                    );
                    outArgsMatch = strncmp(
                        current + 1,
                        currentArg->cstype->varTypeName,
                        currentArgTextLength
                    ) == 0;
                    currentArgTextLength += 2;
                } else if (
                    check_satisfies_expected_output(currentArg->cstype, current)
                ) {
                    outArgsMatch = 1;
                } else {
                    outArgsMatch = 0;
                    break;
                }

                current += currentArgTextLength;
                while (*current == '[' || *current == ']') {
                    if (*current == '[') {
                        dimensionsFound += 1;
                    }
                    current += 1;
                }
                if (dimensionsNeeded != dimensionsFound) {

                    if (isSyntheticVar && dimensionsNeeded < dimensionsFound) {
                        // mismatch in dimensions from synthetic vars
                        // is due to the fact that just walking the tree
                        // it's unable to determine and assign correct
                        // dimensions value. Given the rate resolved fully
                        // thus far, we are safe to fix this here.
                        // ex. `ians = iS + iT where iS and iT are arrays
                        // sadly there's no way around this.
                        currentArg->dimensions = dimensionsFound;
                        CS_VARIABLE* var = csoundFindVariableWithName(
                            csound,
                            typeTable->localPool,
                            currentArg->text
                        );

                        if (var != NULL) {
                            var->dimensions = currentArg->dimensions;
                        } else {
                            printf(Str("error: couldn't find synthetic arg"
                                        " while resolving input args\n"));
                        }
                        outArgsMatch = 1;
                    } else {
                        outArgsMatch = 0;
                        break;
                    }

                }
                car = car->next;
                currentArg = car == NULL ? NULL : (CSOUND_ORC_ARGUMENT*) car->value;
            }
        }

        if (
            inArgsMatch &&
            outArgsMatch &&
            fill_optional_inargs(
                csound,
                inlist,
                typeTable,
                temp
            )
        ) {
            return temp;
        }
        // printf("entry doesn't match in: %s out: %s\n", temp->intypes, temp->outypes);
    }

    return NULL;
}

static CSOUND_ORC_ARGUMENT* new_csound_orc_argument(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENTS* args,
    TREE* tree,
    TYPE_TABLE* typeTable
) {
    CSOUND_ORC_ARGUMENT* arg = csound->Malloc(
        csound,
        sizeof(CSOUND_ORC_ARGUMENT)
    );
    if (tree != NULL && tree->value != NULL) {
        arg->text = csound->Strdup(csound, tree->value->lexeme);
    }
    if (
        tree != NULL &&
        arg->text == NULL &&
        tree->type == T_ARRAY &&
        tree->left != NULL
    ) {
        arg->text = tree->left->value->lexeme;
    }

    if (tree != NULL) {
        arg->type = tree->type;
        arg->linenum = tree->line;
    }

    arg->dimensions = 0;
    arg->dimension = 0;
    arg->isGlobal = 0;
    arg->isPfield = 0;
    arg->isExpression = 0;
    arg->SubExpression = NULL;
    arg->uid = generateUniqueIdent(csound);

    if (arg->text == NULL) {
        arg->text = arg->uid;
    }
    return arg;
}

static CSOUND_ORC_ARGUMENT* copy_csound_orc_argument(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENT* arg
) {
    CSOUND_ORC_ARGUMENT* ans = (CSOUND_ORC_ARGUMENT*)csound->Malloc(
        csound, sizeof(CSOUND_ORC_ARGUMENT)
    );
    ans->text = arg->text != NULL ? csound->Strdup(csound, arg->text) : NULL;
    ans->uid = csound->Strdup(csound, arg->uid);
    ans->cstype = arg->cstype;
    ans->type = arg->type;
    ans->isGlobal = arg->isGlobal;
    ans->isPfield = arg->isPfield;
    ans->isExpression = arg->isExpression;
    ans->dimensions = arg->dimensions;
    ans->dimension = arg->dimension;
    ans->linenum = arg->linenum;
    return ans;
}



static CS_VARIABLE* add_arg_to_pool(
    CSOUND* csound,
    TYPE_TABLE* typeTable,
    char* varName,
    char* annotation,
    int dimensions
) {
    CS_TYPE* type;
    CS_VAR_POOL* pool;
    void* typeArg = NULL;

    char* varName_ = varName;
    if (*varName_ == '#') {
        varName_ += 1;
    }

    int isGlobal = *varName_ == 'g';
    pool = isGlobal ? typeTable->globalPool : typeTable->localPool;

    CS_VARIABLE* var = csoundFindVariableWithName(
        csound,
        pool,
        varName
    );

    if (var != NULL) {
        var->refCount += 1;
    } else {
        if (annotation != NULL) {
            type = csoundGetTypeWithVarTypeName(
                csound->typePool,
                annotation
            );
            typeArg = type;
        } else {
            if (*varName_ == 'g') {
                varName_ += 1;
            }
            type = csoundFindStandardTypeWithChar(varName_[0]);

            if (type == NULL) {
                type = (CS_TYPE*) &CS_VAR_TYPE_L;
            }
        }
        var = csoundCreateVariable(
            csound,
            csound->typePool,
            type,
            varName,
            dimensions,
            typeArg
        );

        csoundAddVariable(csound, pool, var);
    }

    return var;

}

static CSOUND_ORC_ARGUMENTS* concat_orc_arguments(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENTS* args1,
    CSOUND_ORC_ARGUMENTS* args2
) {

    if (args1->length > 0) {
        int pos = args1->length;
        CONS_CELL* car = args1->list;
        while(pos--) {
            if (car->next != NULL) {
                car = car->next;
            }
        }
        if (args2->length > 0) {
            args1->length += args2->length;
            car->next = args2->list;
            // car->next = csound->Malloc(csound, sizeof(CONS_CELL));
            // car->next->value = args2->list;
            // CONS_CELL* car2 = args2->list;
            // int index = 0;
            // while (car2 != NULL) {
            //     printf("index %d\n", index);
            //     CONS_CELL* next = csound->Malloc(csound, sizeof(CONS_CELL));
            //     next->value = copy_csound_orc_argument(
            //         csound,
            //         car2->value
            //     );
            //     car2 = car2->next;
            //     car = next;
            //     car = car->next;
            //     index += 1;
            // }
        }
    }

    return args1;
}

static CSOUND_ORC_ARGUMENT* resolve_single_argument_from_tree(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENTS* args,
    TREE* tree,
    TYPE_TABLE* typeTable,
    int isAssignee
) {
    char* ident;
    char* annotation = NULL;
    CS_VARIABLE* var = NULL;
    CSOUND_ORC_ARGUMENT* arg = new_csound_orc_argument(
        csound,
        args,
        tree,
        typeTable
    );

    if (tree->value != NULL && tree->value->optype != NULL) {
        annotation = tree->value->optype;
    }

    args->append(csound, args, arg);

    // if (tree->type == '?') {
    //     CSOUND_ORC_ARGUMENTS* subExpr = new_csound_orc_arguments(csound);
    //     // ternary expression
    //     // starting with boolean
    //     subExpr = get_arguments_from_tree(
    //         csound,
    //         subExpr,
    //         tree->left,
    //         typeTable,
    //         0
    //     );

    //     if (subExpr == NULL) {
    //         return NULL;
    //     }

    //     CSOUND_ORC_ARGUMENT* booleanExpr =
    //         subExpr->length == 0 ? NULL : subExpr->list->value;

    //     if (
    //         UNLIKELY(
    //             booleanExpr == NULL ||
    //             (booleanExpr->cstype != (CS_TYPE*) &CS_VAR_TYPE_B &&
    //              booleanExpr->cstype != (CS_TYPE*) &CS_VAR_TYPE_b)
    //         )
    //     ) {
    //         synterr(csound,
    //                 Str("non-boolean expression found for ternary operator,"
    //                     " line %d\n"), tree->line);
    //         do_baktrace(csound, tree->locn);
    //         return NULL;
    //     }

    //     // then true (left side)
    //     subExpr = get_arguments_from_tree(
    //         csound,
    //         subExpr,
    //         tree->right->left,
    //         typeTable,
    //         0
    //     );

    //     if (subExpr == NULL) {
    //         return NULL;
    //     }

    //     // then false (right side)
    //     subExpr = get_arguments_from_tree(
    //         csound,
    //         subExpr,
    //         tree->right->right,
    //         typeTable,
    //         0
    //     );

    //     if (subExpr == NULL) {
    //         return NULL;
    //     }

    //     char* opname = csound->Strdup(csound, ":cond");
    //     OENTRIES* entries = find_opcode2(csound, opname);
    //     OENTRY* oentry = resolve_opcode_with_orc_args(
    //         csound,
    //         entries,
    //         subExpr,
    //         NULL,
    //         typeTable,
    //         1
    //     );

    //     if (oentry == NULL) {
    //         CSOUND_ORC_ARGUMENT* leftExpr = subExpr->nth(subExpr, 1);
    //         CSOUND_ORC_ARGUMENT* rightExpr = subExpr->nth(subExpr, 2);
    //         synterr(
    //             csound,
    //             Str("Line %d\n"
    //                 "\tillegal combination of cases"
    //                 " in ternary statement with\n"
    //                 "\t'%s' on left side and '%s' on right side\n"),
    //             tree->line,
    //             leftExpr->cstype->varDescription,
    //             rightExpr->cstype->varDescription
    //         );
    //         return NULL;
    //     }

    //     tree->markup = oentry;
    //     tree->inlist = args;
    //     tree->outlist = NULL;
    //     arg->text = opname;
    //     arg->cstype = csoundFindStandardTypeWithChar('b');
    //     arg->SubExpression = subExpr;
    // } else
    if (
        is_expression_node(tree) ||
        is_boolean_expression_node(tree) ||
        tree->type == STRUCT_EXPR
    ) {
        int isBool = is_boolean_expression_node(tree);
        int isFunCall = tree->type == T_FUNCTION;
        arg->isExpression = 1;

        CSOUND_ORC_ARGUMENTS* subExpr = new_csound_orc_arguments(csound);

        // unary expressions in unexpanded trees need to account
        // for the fact that it only has one node present out of actual two
        int isUnary = is_unary_token_type(tree->type);
        // T_ARRAY as expression
        int isArray = tree->type == T_ARRAY;
        int isStructExpr = tree->type == STRUCT_EXPR;

        if (isArray) {
            var = csoundFindVariableWithName(
                csound,
                typeTable->localPool,
                tree->left->value->lexeme
            );
            var = var == NULL ? csoundFindVariableWithName(
                csound,
                typeTable->globalPool,
                tree->left->value->lexeme
            ) : var;

            if (var == NULL) {
                synterr(
                    csound,
                    Str("Line %d array-identifier '%s' used "
                        "before defined."),
                    tree->line,
                    tree->left->value->lexeme
                );
                return NULL;
            }

            if (
                var->dimensions == 0 &&
                var->varType != (CS_TYPE*) &CS_VAR_TYPE_A
            ) {
                synterr(
                    csound,
                    Str("Line %d variable '%s' isn't an array"),
                    tree->line,
                    tree->left->value->lexeme
                );
                return NULL;
            }

            arg->cstype = var->varType;
        }

        subExpr = tree->left != NULL &&
            tree->left->outlist != NULL ?
            concat_orc_arguments(csound, subExpr, tree->left->outlist) :
            get_arguments_from_tree(
                csound,
                subExpr,
                tree->left,
                typeTable,
                isAssignee
            );

        if (subExpr == NULL) {
            return NULL;
        }

        debugPrintArglist(csound, subExpr, arg, tree);

        if (!isStructExpr) {
            subExpr = tree->right->outlist != NULL ?
                concat_orc_arguments(csound, subExpr, tree->right->outlist) :
                get_arguments_from_tree(
                    csound,
                    subExpr,
                    tree->right,
                    typeTable,
                    isAssignee
                );
            if (subExpr == NULL) {
                return NULL;
            }
        }

        debugPrintArglist(csound, subExpr, arg, tree);

        if (isArray) {
            // attach the array-get indicies to
            // resolve which dimension we are refering to
            // this creates "false" argument which needs
            // to be removed after oentry resolution
            tree->left->right = tree->right;
            subExpr = tree->left->outlist != NULL ? \
                concat_orc_arguments(csound, subExpr, tree->left->outlist) : \
                get_arguments_from_tree(
                    csound,
                    subExpr,
                    tree->left,
                    typeTable,
                    isAssignee
                );
            // remove the pointer to prevent incorrect behavior later
            subExpr->remove_nth(csound, subExpr, 2);
            tree->left->right = NULL;
        }


        if (subExpr == NULL) {
            return NULL;
        }

        arg->SubExpression = subExpr;
        OENTRY* oentry = NULL;

        // if (isStructExpr) {
        //     // for member setters and getters there's
        //     // only 1 oentry for each, we save the trouble
        //     // of creating a temporary fake argument like
        //     // we do with arrays
        //     oentry = isAssignee ?
        //         find_opcode(csound, "##member_set") :
        //         find_opcode(csound, "##member_get");
        //     goto expression_end;
        // }

        char* opname = NULL;

        if (isFunCall) {
            opname = tree->value->lexeme;
            // allow for fn([])
            if (!isAssignee && strncmp("fillarray", tree->value->lexeme, 9) == 0) {
                arg->dimensions = 1;
            }
        } else if (isBool) {
            opname = get_boolean_expression_opcode_type(csound, tree);
        } else {
            opname = get_expression_opcode_type(csound, tree);
        }

        OENTRIES* entries = find_opcode2(csound, opname);

        if (UNLIKELY(entries == NULL || entries->count == 0)) {
            synterr(
                csound,
                Str("Line %d unable to find opcode '%s'"),
                tree->line - 1,
                opname
            );
            if (entries != NULL) {
                csound->Free(csound, entries);
            }
            return NULL;
        }
        // print_tree(csound, "i()", tree);
        debugPrintArglist(csound, arg->SubExpression, arg, tree);

        oentry = resolve_opcode_with_orc_args(
            csound,
            entries,
            arg->SubExpression,
            NULL,
            typeTable,
            1
        );

        if (oentry == NULL) {
            synterr(
                csound,
                Str("Line %d no arguments match opcode '%s'"),
                tree->line - 1,
                opname
            );
            return NULL;
        }

        expression_end:
        tree->markup = oentry;
        tree->inlist = arg->SubExpression;
        tree->outlist = args;
        if (arg->text == NULL) {
            arg->text = opname;
        }
        if (arg->cstype == NULL) {
            arg->cstype = csoundFindStandardTypeWithChar(*oentry->outypes);
        }

        var = csoundCreateVariable(
            csound,
            csound->typePool,
            arg->cstype,
            arg->text,
            0,
            annotation
        );
        // TODO handle global vars
        csoundAddVariable(csound, typeTable->localPool, var);
        return arg;
    }

    switch(tree->type) {
        case NUMBER_TOKEN:
        case INTEGER_TOKEN: {
            arg->cstype = (CS_TYPE*) &CS_VAR_TYPE_C;
            break;
        }
        case STRING_TOKEN: {
            arg->cstype = (CS_TYPE*) &CS_VAR_TYPE_S;
            break;
        }
        case LABEL_TOKEN: {
            arg->cstype = (CS_TYPE*) &CS_VAR_TYPE_L;
            break;
        }
        case T_MEMBER_IDENT: {
            arg->cstype = (CS_TYPE*) &CS_VAR_TYPE_C;
            tree->type = INTEGER_TOKEN;
            CS_VARIABLE* structVar = csoundFindVariableWithName(
                csound,
                typeTable->localPool,
                tree->value->optype
            );
            if (structVar == NULL) {
                structVar = csoundFindVariableWithName(
                    csound,
                    csound->engineState.varPool,
                    tree->value->optype
                );
            }
            if (structVar == NULL) {
                synterr(
                    csound,
                    Str("Line %d failed to find member '%s' of "
                        "non existing struct variable '%s'"),
                    tree->line,
                    tree->value->lexeme,
                    tree->value->optype
                );
                return NULL;
            }

            int memberIndex = findStructMemberIndex(
                structVar->varType->members,
                tree->value->lexeme
            );

            if (memberIndex > -1) {
                char* numStr = csound->Calloc(csound, 16 * sizeof(char));
                sprintf(numStr, "%d", memberIndex);
                csound->Free(csound, tree->value->lexeme);
                tree->value->lexeme = numStr;
                tree->value->value = memberIndex;
            } else {
                synterr(
                    csound,
                    Str("Line %d member '%s' doesn't exist in struct '%s'"),
                    tree->line,
                    tree->value->lexeme,
                    structVar->varType->varTypeName
                );
                return NULL;
            }
            // arg->text = csound->Strdup(csound, "1");
            // tree->value->type = INTEGER_TOKEN;
            // tree->value->lexeme = csound->Strdup(csound, "666");
            // tree->value->fvalue = FL(666.0);

            break;
        }
        case T_ARRAY_IDENT: {
            if (
                tree->right != NULL &&
                tree->right->value != NULL &&
                *tree->right->value->lexeme == '['
            ) {
                TREE* currentBracketNode = tree->right;

                while (currentBracketNode != NULL) {
                    arg->dimensions += 1;
                    currentBracketNode = currentBracketNode->next;
                }
            }
            // lack of break is intended
        }
        case T_TYPED_IDENT:
        case T_IDENT: {
            ident = csound->Strdup(csound, tree->value->lexeme);
            // let's first check if it already exists
            var = csoundFindVariableWithName(
                csound,
                typeTable->localPool,
                ident
            );
            if (var == NULL) {
                var = csoundFindVariableWithName(
                    csound,
                    typeTable->globalPool,
                    ident
                );
                arg->isGlobal = var != NULL;
            }

            if (var != NULL) {
                // already defined
                arg->cstype = var->varType;
                arg->dimension = var->dimension;
                arg->dimensions = var->dimensions;
                break;
            }

            if (is_reserved(ident)) {
                arg->cstype = (CS_TYPE*) &CS_VAR_TYPE_R;
                break;
            }
            if (is_label(ident, typeTable->labelList)) {
                arg->cstype = (CS_TYPE*) &CS_VAR_TYPE_L;
                return arg;
            }
            if (
                (*ident >= '1' && *ident <= '9') ||
                *ident == '.' || *ident == '-' || *ident == '+' ||
                (*ident == '0' && strcmp(ident, "0dbfs") != 0)
            ) {
                arg->cstype = (CS_TYPE*) &CS_VAR_TYPE_C;
                break;
            }
            if (*ident == '"') {
                arg->cstype = (CS_TYPE*) &CS_VAR_TYPE_S;
                break;
            }
            if (pnum(ident) >= 0) {
                arg->cstype = (CS_TYPE*) &CS_VAR_TYPE_P;
                arg->isPfield = 1;

                csoundAddVariable(
                    csound,
                    typeTable->localPool,
                    csoundCreateVariable(
                        csound,
                        csound->typePool,
                        arg->cstype,
                        ident,
                        arg->dimensions,
                        NULL
                    )
                );
                break;
            }
            if (*ident == 'g' || is_reserved(ident)) {
                arg->isGlobal = 1;
            }

            if (isAssignee && ident[0] != '#') {
                if (
                    annotation == NULL &&
                    arg->cstype == NULL &&
                    is_legacy_t_rate_ident(ident)
                ) {
                    arg->cstype = (CS_TYPE*)&CS_VAR_TYPE_K;
                    arg->dimensions = 1;
                    csound->Warning(
                        csound,
                        Str("Using t-rate variables is deprecated; use k[] instead\n")
                    );
                } else {
                    arg->cstype = csoundFindStandardTypeWithChar(
                        *ident == 'g' ? ident[1] : ident[0]
                    );
                }

                if (annotation == NULL && arg->cstype == NULL) {
                    tree->type = T_UNKNOWN_IDENT;
                    return arg;
                }

                var = add_arg_to_pool(
                    csound,
                    typeTable,
                    ident,
                    annotation,
                    arg->dimensions
                );
                arg->cstype = var->varType;
                arg->dimensions = var->dimensions;

                if (arg->type == T_ARRAY_IDENT) break;

                // count the dimension nesting
                if (arg->dimensions > 0) {
                    TREE* arrayIndexGetLeaf = tree->right;
                    while (arrayIndexGetLeaf != NULL) {
                        arg->dimension += 1;
                        arrayIndexGetLeaf = arrayIndexGetLeaf->next;
                    }
                }
                goto count_dim;
            }

            if (var != NULL) {
                arg->cstype = var->varType;
                arg->dimensions = var->dimensions;
                goto count_dim;
            }

            if (arg->isGlobal) {
                var = csoundFindVariableWithName(
                    csound,
                    csound->engineState.varPool,
                    ident
                );

                if (var == NULL) {
                    var = csoundFindVariableWithName(
                        csound,
                        typeTable->globalPool,
                        ident
                    );
                }
            } else {
                var = csoundFindVariableWithName(
                    csound,
                    typeTable->localPool,
                    tree->value->lexeme
                );
            }

            if (var == NULL) {
                // last attempt, find the var from
                // UDO xin scope
                var = csoundFindVariableWithName(
                    csound,
                    typeTable->instr0LocalPool,
                    tree->value->lexeme
                );
            }

            count_dim:

            if (UNLIKELY(var == NULL)) {
                synterr(
                    csound,
                    Str("\nLine %d variable '%s' used before defined\n"),
                    tree->line - 1,
                    tree->value->lexeme
                );
                return NULL;
            }

            // a variable can be initialized from a pre-defined
            // array at a different dimension, so we start counting
            // from where the variable left off
            arg->dimension = var->dimension;
            // count the dimension nesting
            if (arg->dimensions > 0 && !isAssignee) {
                TREE* arrayIndexGetLeaf = tree->right;
                while (arrayIndexGetLeaf != NULL) {
                    arg->dimension += 1;
                    arrayIndexGetLeaf = arrayIndexGetLeaf->next;
                }
            }

            if (arg->cstype == NULL) {
                arg->cstype = var->varType;
            }

            break;
        }
    }

    return arg;
}

static CSOUND_ORC_ARGUMENTS* get_arguments_from_tree(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENTS* args,
    TREE* tree,
    TYPE_TABLE* typeTable,
    int isAssignee
) {
    CSOUND_ORC_ARGUMENT* result;
    TREE* current = tree;

    while (current != NULL) {
        result = resolve_single_argument_from_tree(
            csound,
            args,
            current,
            typeTable,
            isAssignee
        );

        if (result == NULL) {
            return NULL;
        }

        current = current->next;
    }
    return args;
}

// CSOUND_ORC_ARGUMENTS helper function
// get the nth index from argument list
static CSOUND_ORC_ARGUMENT* arglist_nth(
    CSOUND_ORC_ARGUMENTS* args,
    int nth
) {
    if (nth < 0 || args->length <= nth) {
        return NULL;
    }
    CONS_CELL* car = args->list;
    while(nth > 0 && nth--) {
        car = car->next;
    }
    if (car == NULL) {
        return NULL;
    } else {
        return (CSOUND_ORC_ARGUMENT*) car->value;
    }
}

// CSOUND_ORC_ARGUMENTS helper function
static void arglist_append(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENTS* args,
    CSOUND_ORC_ARGUMENT* arg
) {
    CONS_CELL* tail = csound->Malloc(csound, sizeof(CONS_CELL));
    tail->value = arg;
    tail->next = NULL;

    if (args->list == NULL) {
        args->list = tail;
    } else {
        int pos = args->length;
        CONS_CELL* car = args->list;
        while(pos--) {
            if (car->next != NULL) {
                car = car->next;
            }
        }
        car->next = tail;
    }
    args->length += 1;
}

static void arglist_remove_nth(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENTS* args,
    int nth
) {
    if (nth < 0 || args->length <= nth) {
        printf("Internal error: tried to remove non-existing arglist entry\n");
        return;
    }
    CONS_CELL* lastCar = NULL;
    CONS_CELL* car = args->list;
    int index = 0;
    while(index < args->length) {
        if (index == nth) {
            args->length -= 1;
            if (lastCar != NULL) {
                lastCar->next = car->next;
            }
            csound->Free(csound, car);
            return;
        }
        lastCar = car;
        car = car == NULL ? NULL : car->next;
        index += 1;
    }
}

// generic
CSOUND_ORC_ARGUMENTS* new_csound_orc_arguments(CSOUND* csound) {
    CSOUND_ORC_ARGUMENTS* args = csound->Malloc(
        csound,
        sizeof(CSOUND_ORC_ARGUMENTS)
    );
    args->list = NULL;
    args->length = 0;
    args->nth = &arglist_nth;
    args->append = &arglist_append;
    args->remove_nth = &arglist_remove_nth;

    return args;
}


// for exception cases where the opcode search
// will find wrong onetry, ex. when lexeme
// str not giving all the required hints for
// correct resolution.
OENTRY* maybe_fix_opcode_resolution(
    CSOUND* csound,
    CSOUND_ORC_ARGUMENTS* argsLeft,
    CSOUND_ORC_ARGUMENTS* argsRight,
    TYPE_TABLE* typeTable,
    char* opcodeString
) {

    if (
        *opcodeString == '=' &&
        argsLeft->length == 1
    ) {
        CSOUND_ORC_ARGUMENT* outarg = argsLeft->list->value;
        if (outarg->dimensions > 0) {
            return find_opcode(csound, "##array_assign");
        }
    }

    return NULL;
}

static OENTRY* resolve_opcode_with_unknown_output(
    CSOUND* csound,
    OENTRIES* entries,
    CSOUND_ORC_ARGUMENTS* leftSideArgs,
    CSOUND_ORC_ARGUMENTS* rightSideArgs,
    TREE* left,
    TREE* right,
    TYPE_TABLE* typeTable
) {
    CSOUND_ORC_ARGUMENT* outputArg = new_csound_orc_argument(
        csound,
        leftSideArgs,
        left,
        typeTable
    );
    CSOUND_ORC_ARGUMENT* firstRightArg = rightSideArgs->list->value;
    // we can safely resolve struct outtype now
    if (firstRightArg->cstype->userDefinedType) {
        int memberIndex = right->next->value->value;
        CONS_CELL* car = firstRightArg->cstype->members;
        while (memberIndex) {
            car = car->next;
            memberIndex -= 1;
        }
        CS_VARIABLE* memberVar = (CS_VARIABLE*) car->value;
        CS_VARIABLE* pointerVar = csoundCreateVariable(
            csound,
            csound->typePool,
            memberVar->varType,
            left->value->lexeme,
            memberVar->dimensions,
            NULL
        );
        memberVar->refCount += 1;
        csoundAddVariable(
            csound,
            typeTable->localPool,
            pointerVar
        );
        left->type = T_IDENT;
        outputArg->cstype = pointerVar->varType;
        leftSideArgs->append(csound, leftSideArgs, outputArg);

    }

    // if the outargs match, we know will know
    // the actual type of uknown, given the opcode
    // has non-empty outlist
    OENTRY* oentry = resolve_opcode_with_orc_args(
        csound,
        entries,
        rightSideArgs,
        leftSideArgs,
        typeTable,
        1
    );

    if (oentry && firstRightArg->cstype->userDefinedType) {
        return oentry;
    }
    if (oentry && strlen(oentry->outypes) > 0) {
        CS_TYPE* standardType = csoundFindStandardTypeWithChar(
            *oentry->outypes
        );

        if (standardType == NULL) {
            if (is_internal_array_opcode(oentry->opname)) {
                standardType = firstRightArg->cstype;
            }

        }

        if (standardType == NULL) {
            printf(
                "Internal error: standardType wasn't found for '%c'\n",
                *oentry->outypes);
        }

        int dimensions = 0;
        if (strlen(oentry->outypes) > 1 && oentry->outypes[1] == '[') {
            // FIXME multi-dimensions
            dimensions = 1;
        }

        outputArg->cstype = standardType;
        outputArg->dimensions = dimensions;

        csoundAddVariable(
            csound,
            typeTable->localPool,
            csoundCreateVariable(
                csound,
                csound->typePool,
                standardType,
                left->value->lexeme,
                dimensions,
                NULL
            )
        );
        left->type = T_IDENT;
        leftSideArgs->append(csound, leftSideArgs, outputArg);

        return oentry;
    }

    printf("Internal error: FIXME (no opcode resolved)\n");

    return NULL;
}

int verify_opcode_2(
    CSOUND* csound,
    TREE* root,
    TYPE_TABLE* typeTable
) {
    OENTRIES* entries = find_opcode2(csound, root->value->lexeme);

    if (UNLIKELY(entries == NULL || entries->count == 0)) {
        synterr(
            csound,
            Str("Line %d unable to find opcode '%s'"),
            root->line,
            root->value->lexeme
        );

        if (entries != NULL) {
            csound->Free(csound, entries);
        }
        return 0;
    }

    CSOUND_ORC_ARGUMENTS* leftSideArgs = new_csound_orc_arguments(csound);
    CSOUND_ORC_ARGUMENTS* rightSideArgs = new_csound_orc_arguments(csound);

    OENTRY* oentry = NULL;
    TREE* left = root->left;
    TREE* right = root->right;

    rightSideArgs = get_arguments_from_tree(
        csound,
        rightSideArgs,
        right,
        typeTable,
        0
    );

    if (rightSideArgs == NULL) {
        return 0;
    }

    if (left != NULL && left->type == T_UNKNOWN_IDENT) {
        oentry = resolve_opcode_with_unknown_output(
            csound,
            entries,
            leftSideArgs,
            rightSideArgs,
            left,
            right,
            typeTable
        );
        goto validate_oentry;
    }

    leftSideArgs = get_arguments_from_tree(
        csound,
        leftSideArgs,
        left,
        typeTable,
        1
    );

    if (leftSideArgs == NULL) {
        return 0;
    }

    // check if the inargs lookup had to declare
    // the tree node unknown type
    if (left != NULL && left->type == T_UNKNOWN_IDENT) {
        oentry = resolve_opcode_with_unknown_output(
            csound,
            entries,
            leftSideArgs,
            rightSideArgs,
            left,
            right,
            typeTable
        );
        goto validate_oentry;
    }

    // printOrcArgs(leftSideArgs);
    // printOrcArgs(rightSideArgs);
    if (oentry == NULL) {
        oentry = maybe_fix_opcode_resolution(
            csound,
            leftSideArgs,
            rightSideArgs,
            typeTable,
            root->value->lexeme
        );
    }

    if (oentry == NULL) {
        oentry = resolve_opcode_with_orc_args(
            csound,
            entries,
            rightSideArgs,
            leftSideArgs,
            typeTable,
            0
        );
    }

    validate_oentry:
    if (UNLIKELY(oentry == NULL)) {
        synterr(
            csound,
            Str("Unable to find opcode entry for \'%s\' "
                "with matching argument types:\n"),
            root->value->lexeme
        );

        char* leftArglist = make_comma_sep_arglist(csound, leftSideArgs, 1);
        char* rightArglist = make_comma_sep_arglist(csound, rightSideArgs, 1);

        csoundMessage(
            csound,
            Str("Found:\n  %s %s %s\n"),
            leftArglist,
            root->value->lexeme,
            rightArglist
        );

        csound->Free(csound, leftArglist);
        csound->Free(csound, rightArglist);

        csoundMessage(csound, Str("\nCandidates:\n"));

        for (int i = 0; i < entries->count; i++) {
            OENTRY *entry = entries->entries[i];
            csoundMessage(
                csound,
                "  %s %s %s\n",
                entry->outypes,
                entry->opname,
                entry->intypes
            );
        }

        csoundMessage(
            csound,
            Str("\nLine: %d\n"),
            root->line
        );
        printOrcArgs(rightSideArgs );
        do_baktrace(csound, root->locn);

        return 0;
    }

    root->markup = oentry;
    root->inlist = rightSideArgs;
    root->outlist = leftSideArgs;


    return 1;

}
