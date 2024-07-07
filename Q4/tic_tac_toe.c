#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BOARD_SIZE 9

void print_board(char board[BOARD_SIZE]);
int validate_input(char *strategy);
int check_win(char board[BOARD_SIZE], char player);
int count_free_spaces(char board[BOARD_SIZE]);
int get_human_move(char board[BOARD_SIZE]);
int get_computer_move(char board[BOARD_SIZE], int moves[9]);

void print_board(char board[BOARD_SIZE]) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        printf("%c ", board[i]);
        if (i % 3 == 2) printf("\n");
    }
}

int validate_input(char *strategy) {
    if (strlen(strategy) != 9) return 0;
    int seen[10] = {0};
    for (int i = 0; i < 9; i++) {
        if (strategy[i] < '1' || strategy[i] > '9') return 0;
        if (seen[strategy[i] - '0']++) return 0;
    }
    return 1;
}

int check_win(char board[BOARD_SIZE], char player) {
    int wins[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 8}, 
        {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, 
        {0, 4, 8}, {2, 4, 6}  
    };
    for (int i = 0; i < 8; i++) {
        if (board[wins[i][0]] == player && board[wins[i][1]] == player && board[wins[i][2]] == player) {
            return 1;
        }
    }
    return 0;
}

int count_free_spaces(char board[BOARD_SIZE]) {
    int count = 0;
    for (int i = 0; i < BOARD_SIZE; i++) {
        if (board[i] != 'X' && board[i] != 'O') {
            count++;
        }
    }
    return count;
}

int get_human_move(char board[BOARD_SIZE]) {
    int human_move;
    char input[10];
    fgets(input, 10, stdin);
    human_move = atoi(input);
    while (human_move < 1 || human_move > 9 || board[human_move - 1] == 'X' || board[human_move - 1] == 'O') {
        printf("Invalid move, try again: ");
        fgets(input, 10, stdin);
        human_move = atoi(input);
    }
    return human_move - 1;
}

int get_computer_move(char board[BOARD_SIZE], int moves[9]) {
    int move = -1;
    for (int i = 0; i < 9; i++) {
        if (board[moves[i]] != 'X' && board[moves[i]] != 'O') {
            move = moves[i];
            break;
        }
    }
    if (count_free_spaces(board) == 1) {
        for (int i = 8; i >= 0; i--) {
            if (board[moves[i]] != 'X' && board[moves[i]] != 'O') {
                move = moves[i];
                break;
            }
        }
    }
    return move;
}

int main(int argc, char *argv[]) {
    printf("Starting tic_tac_toe...\n");
    fflush(stdout);

    if (argc != 2 || !validate_input(argv[1])) {
        printf("Error\n");
        fflush(stdout);
        return 1;
    }

    printf("Received strategy: %s\n", argv[1]);
    fflush(stdout);

    char *strategy = argv[1];
    char board[BOARD_SIZE] = {'1','2','3','4','5','6','7','8','9'};
    int moves[9];

    for (int i = 0; i < 9; i++) {
        moves[i] = strategy[i] - '0' - 1;
    }

    int move_count = 0;
    char player = 'X';

    while (move_count < 9) {
        int move;
        if (player == 'X') {
            move = get_computer_move(board, moves);
            board[move] = player;
            printf("%d\n", move + 1);
            fflush(stdout);
        } else {
            move = get_human_move(board);
            board[move] = player;
        }
        move_count++;

        if (check_win(board, player)) {
            if (player == 'X') {
                printf("I win\n");
                fflush(stdout);
            } else {
                printf("I lost\n");
                fflush(stdout);
            }
            return 0;
        }

        player = (player == 'X') ? 'O' : 'X';

        if (move_count == 9) break;
    }

    printf("DRAW\n");
    fflush(stdout);
    return 0;
}
