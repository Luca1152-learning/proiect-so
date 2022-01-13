// To compile use: clang++ -std=c++11 daemon.cpp -o daemon
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <unordered_map>
#include <string.h>
#include <unistd.h>

using namespace std;

const int MAX_LIST_SIZE = 5000;
const int MTX_LIST = 335;
const int MTX_GRANT = 336;

unordered_map<int, int> id_proc;
int last_grant[MAX_LIST_SIZE][MAX_LIST_SIZE];

int main(int argc, char **argv) {
    // Create a daemon
    pid_t process_id = 0;
    pid_t sid = 0;

    // Create the child process
    process_id = fork();

    // Failed fork()
    if (process_id < 0) {
        printf("fork failed!\n");
        exit(1);
    }

    if (process_id > 0) {
        // Kill the parent
        printf("PID of child process %d \n", process_id);
        exit(0);
    }

    umask(0);
    sid = setsid();
    if (sid < 0) {
        exit(1);
    }
    chdir("/");

    // Close stdin, stdout, stderr
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    pid_t *procs = (pid_t *) malloc(MAX_LIST_SIZE * sizeof(pid_t));
    int *d_mtx = (int *) malloc(MAX_LIST_SIZE * sizeof(int));
    pid_t *threads = (pid_t *) malloc(MAX_LIST_SIZE * sizeof(pid_t));
    memset(last_grant, 0, sizeof(last_grant));

    int current_iteration = 0;

    while (1) {
        sleep(0); // Let the other threads run

        int i;
        int pair_number = syscall(MTX_LIST, procs, d_mtx, threads, MAX_LIST_SIZE), proc_nr = 0;
        for (i = 1; i < pair_number; ++i) {
            proc_nr++;
            id_proc[procs[i]] = proc_nr;
        }

        // Randomize the received list
        i = 0;
        time_t t;
        srand((unsigned) time(&t));
        while (i < pair_number) {
            int index_to_swap = rand() % i;
            int aux = d_mtx[index_to_swap];
            d_mtx[index_to_swap] = d_mtx[i];
            d_mtx[i] = aux;

            aux = threads[index_to_swap];
            threads[index_to_swap] = threads[i];
            threads[i] = aux;

            aux = procs[index_to_swap];
            procs[index_to_swap] = procs[i];
            procs[i] = aux;

            i++;
        }

        // Update the current iteration
        current_iteration++;

        // Grant one thread per mutex per process
        i = 0;
        while (i < pair_number) {
            if (last_grant[id_proc[procs[i]]][d_mtx[i]] != current_iteration) {
                last_grant[id_proc[procs[i]]][d_mtx[i]] = current_iteration;
                syscall(MTX_GRANT, procs[i], d_mtx[i], threads[i]);
            }
            i++;
        }
    }
    return 0;
}