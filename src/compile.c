#include "compile.h"

#include <stdio.h>
#include <stdlib.h>

int if_counter = 0;
int while_counter = 0;

// FOLDING
value_t constant_fold(node_t *node, bool *is_constant) {
    if (node->type == BINARY_OP) {
        binary_node_t *binary_node = (binary_node_t *) node;

        bool left_is_constant, right_is_constant;
        value_t left_val = constant_fold(binary_node->left, &left_is_constant);
        value_t right_val = constant_fold(binary_node->right, &right_is_constant);

        if (left_is_constant && right_is_constant) {
            switch (binary_node->op) {
                case '+':
                    *is_constant = true;
                    return left_val + right_val;
                case '-':
                    *is_constant = true;
                    return left_val - right_val;
                case '*':
                    *is_constant = true;
                    return left_val * right_val;
                case '/':
                    if (right_val != 0) {
                        *is_constant = true;
                        return left_val / right_val;
                    }
                    break;
                default:
                    break;
            }
        }
    }
    else if (node->type == NUM) {
        *is_constant = true;
        return ((num_node_t *) node)->value;
    }

    *is_constant = false;
    return 0; // Placeholder value for non-constants.
}

bool compile_ast(node_t *node) {
    node_type_t node_type = node->type;
    switch (node_type) {
        case NUM: {
            num_node_t *num_node = (num_node_t *) node;
            printf("mov $%ld, %%rdi\n", num_node->value);
            return true;
            break;
        }

        case PRINT: {
            print_node_t *print_node = (print_node_t *) node;
            compile_ast(print_node->expr);
            printf("call print_int\n");
            return true;
            break;
        }

        case SEQUENCE: {
            sequence_node_t *sequence_node = (sequence_node_t *) node;
            for (size_t index = 0; index < sequence_node->statement_count; index++) {
                compile_ast(sequence_node->statements[index]);
            }
            return true;
            break;
        }

        case BINARY_OP: {
            binary_node_t *binary_node = (binary_node_t *) node;

            // FOLDING
            bool is_constant = false;
            value_t constant_val = constant_fold(node, &is_constant);
            if (is_constant) {
                printf("mov $%ld, %%rdi\n", constant_val);
                return true;
            }

            // added
            if (binary_node->op == '*' && binary_node->right->type == NUM) {
                num_node_t *num_node = (num_node_t *) binary_node->right;
                if (num_node->value > 0 &&
                    (num_node->value & (num_node->value - 1)) == 0) {
                    compile_ast(binary_node->left);
                    int shift_amount = 0;
                    int64_t value = num_node->value;
                    while (value >>= 1) {
                        shift_amount++;
                    }
                    printf("shl $%d, %%rdi\n", shift_amount);
                    return true;
                }
            }

            compile_ast(binary_node->right);
            printf("push %%rdi\n");
            compile_ast(binary_node->left);
            printf("pop %%r8\n");

            //    compile_ast(binary_node->left); // compile left side of binary
            //    operation, result is in %rdi from NUM case
            //    // rdi = left, r8 = , stack =
            //    printf("push %%rdi\n"); // push result on %rdi to stack to save it
            //    // rdi = left, r8 = , stack = left
            //    compile_ast(binary_node->right); // compile right side of binary
            //    operation, result is in %rdi from NUM case
            //    // rdi = right, r8 = , stack = left
            //    printf("mov %%rdi, %%r8\n");
            //    // rdi = right, r8 = right, stack = left
            //    printf("pop %%rdi\n"); // pops top value off stack and puts in %rdi
            //    // rdi = left, r8 = right, stack =

            // have result of left child in %rdi and right child in %r8, now perform
            // operations
            if (binary_node->op == '+') {
                printf("addq %%r8, %%rdi\n");
                return true;
            }
            else if (binary_node->op == '*') {
                // check if r8 is constant
                if (binary_node->right->type == NUM) {
                    num_node_t *num_node = (num_node_t *) binary_node->right;
                    // if r8 is > 0 and even
                    if (num_node->value > 0 &&
                        (num_node->value & (num_node->value - 1)) == 0) {
                        int shift_amount = 0;
                        int64_t value = num_node->value;
                        while (value >>= 1) {
                            shift_amount++;
                        }
                        printf("shl $%d, %%rdi\n", shift_amount);
                        return true;
                    }
                }
                // if we haven't returned yet, it means either the right node isn't a NUM
                // or it's not a power of two.
                printf("imulq %%r8, %%rdi\n");
                return true;
            }
            else if (binary_node->op == '-') {
                printf("subq %%r8, %%rdi\n");
                return true;
            }
            else if (binary_node->op == '/') {
                printf("mov %%rdi, %%rax\n"); // move dividend (left of binary op) to %rax
                printf("cqto\n");      // extends size of %rax (quadword to octaword)
                printf("idiv %%r8\n"); // divide %rdx::%rax by r8
                printf("mov %%rax, %%rdi\n"); // move quotient to %rdi
                return true;
            }
            break;
        }

        case VAR: {
            var_node_t *var_node = (var_node_t *) node;
            printf("mov -0x%02x(%%rbp), %%rdi\n", 8 * (var_node->name - 'A' + 1));
            return true;
            break;
        }

        case LET: {
            let_node_t *let_node = (let_node_t *) node;
            compile_ast(let_node->value);
            printf("mov %%rdi, -0x%02x(%%rbp)\n", 8 * (let_node->var - 'A' + 1));
            return true;
            break;
        }

        case IF: {
            if_node_t *if_node = (if_node_t *) node;
            if_counter++;
            int i = if_counter;
            compile_ast(
                (node_t *) if_node
                    ->condition); // compiles condition of if_node, will go to
                                  // BINARY_OP case, saves results in register (%rdi)

            binary_node_t *binary_node =
                (binary_node_t *) if_node
                    ->condition; // generates different asm code for different conditions
            // with following switch statement
            char op = binary_node->op;

            // Check if there's an else branch
            bool has_else = if_node->else_branch != NULL;

            // need to do cmp binary_node->left, binary_node->right
            printf("cmp %%r8, %%rdi\n");

            switch (op) {
                case '=': {
                    printf("jne %s%d\n", has_else ? "IF_ELSE" : "IF_END",
                           i); // asm instruction to jump if not equal, otherwise continue
                               // execution
                    break;
                }
                case '<': {
                    printf("jnl %s%d\n", has_else ? "IF_ELSE" : "IF_END", i);
                    break;
                }
                case '>': {
                    printf("jng %s%d\n", has_else ? "IF_ELSE" : "IF_END", i);
                    break;
                }
            }
            printf("IF%d:\n", i);            // label for IF branch
            compile_ast(if_node->if_branch); // compile IF branch
            printf("jmp IF_END%d\n",
                   i); // jump instruction to IF_END to skip potential else branch

            // Only generate the ELSE label and branch if there is an else statement
            if (has_else) {
                printf("IF_ELSE%d:\n", i); //  label for ELSE branch, only jumps to this
                                           //  if condition is False
                compile_ast((node_t *) if_node->else_branch);
            }
            printf("IF_END%d:\n", i);
            return true;
            break;
        }

        case WHILE: {
            while_node_t *while_node = (while_node_t *) node;
            size_t cur = while_counter++;

            char while_condition[32];
            sprintf(while_condition, "WHILE_CONDITION%zu", cur);

            char while_body[32];
            sprintf(while_body, "WHILE_BODY%zu", cur);

            char while_end[32];
            sprintf(while_end, "WHILE_END%zu", cur);

            printf("jmp %s\n", while_condition); // jump to check while condition

            printf("%s:\n", while_body);
            compile_ast((node_t *) while_node->body);

            printf("%s:\n", while_condition);
            compile_ast((node_t *) while_node->condition);

            printf("cmp %%r8, %%rdi\n"); // sets up flags for following jumps

            binary_node_t *binary_node = (binary_node_t *) while_node->condition;
            char op = binary_node->op;

            switch (op) {
                case '=':
                    printf("je %s\n", while_body);
                    break;
                case '<':
                    printf("jl %s\n", while_body);
                    break;
                case '>':
                    printf("jg %s\n", while_body);
                    break;
            }

            // jumps to this label if cmp r8 rdi results in false
            printf("%s:\n", while_end);

            return true;
            break;
        }
    }
    return false; // for now, every statement causes a compilation failure
}
