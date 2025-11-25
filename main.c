#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/sem.h>

#define NUM_QUESTIONS 5
#define MAX_LINE 256
#define SEM_RUBRIC 0
#define SEM_EXAM   1
#define SEM_QBASE  2  // question semaphores start at index 2

// Structure stored in shared memory
typedef struct {
    char rubric[NUM_QUESTIONS][MAX_LINE];
    int current_exam_student;
    int question_marked[NUM_QUESTIONS];
    int exam_index;
} shared_data_t;

// semaphore buffer for semop()
struct sembuf P = {0, -1, 0};  // wait
struct sembuf V = {0, 1, 0};   // signal

// RANDOM DELAY
void random_delay(double min, double max) {
    double secs = min + ((double)rand() / RAND_MAX) * (max - min);
    usleep((int)(secs * 1e6));
}

// P() operation
void sem_wait(int semid, int semnum) {
    struct sembuf op = {semnum, -1, 0};
    semop(semid, &op, 1);
}

// V() operation
void sem_signal(int semid, int semnum) {
    struct sembuf op = {semnum, +1, 0};
    semop(semid, &op, 1);
}

// Load rubric from file
void load_rubric(shared_data_t *sh) {
    FILE *f = fopen("rubric.txt", "r");
    if (!f) { perror("rubric.txt"); exit(1); }

    for (int i = 0; i < NUM_QUESTIONS; i++) {
        fgets(sh->rubric[i], MAX_LINE, f);
        sh->rubric[i][strcspn(sh->rubric[i], "\n")] = '\0';
    }
    fclose(f);
}

// Save rubric to file
void save_rubric(shared_data_t *sh) {
    FILE *f = fopen("rubric.txt", "w");
    if (!f) return;

    for (int i = 0; i < NUM_QUESTIONS; i++)
        fprintf(f, "%s\n", sh->rubric[i]);

    fclose(f);
}

// Load exam file
int load_exam(shared_data_t *sh, int exam_num) {
    char filename[64];
    sprintf(filename, "exam%d.txt", exam_num);

    FILE *f = fopen(filename, "r");
    if (!f) return 0;

    char line[MAX_LINE];
    fgets(line, MAX_LINE, f);
    sh->current_exam_student = atoi(line);

    for (int i = 0; i < NUM_QUESTIONS; i++)
        sh->question_marked[i] = 0;

    fclose(f);
    return 1;
}

// TA PROCESS
void TA_process(int id, shared_data_t *sh, int semid) {
    srand(time(NULL) ^ getpid());

    while (1) {
        int student = sh->current_exam_student;

        if (student == 9999) {
            printf("TA %d: Final exam reached — stopping.\n", id);
            exit(0);
        }

        printf("TA %d: Reviewing rubric for student %d...\n", id, student);

        // Protect rubric using SEM_RUBRIC
        sem_wait(semid, SEM_RUBRIC);

        for (int i = 0; i < NUM_QUESTIONS; i++) {
            random_delay(0.5, 1.0);
            if (rand() % 2) {
                char *comma = strchr(sh->rubric[i], ',');
                if (comma && comma[2]) {
                    comma[2] = comma[2] + 1;
                    printf("TA %d: Corrected rubric line %d → %c\n",
                           id, i+1, comma[2]);
                }
            }
        }
        save_rubric(sh);
        sem_signal(semid, SEM_RUBRIC);

        // MARK QUESTIONS
        for (int q = 0; q < NUM_QUESTIONS; q++) {

            sem_wait(semid, SEM_QBASE + q);

            if (sh->question_marked[q] == 0) {
                sh->question_marked[q] = 1;
                printf("TA %d: Marking question %d for student %d...\n",
                       id, q+1, student);
                random_delay(1.0, 2.0);
                printf("TA %d: Finished marking Q%d for student %d.\n",
                       id, q+1, student);
            }

            sem_signal(semid, SEM_QBASE + q);
        }

        // TA 0 loads next exam (protected)
        if (id == 0) {
            sem_wait(semid, SEM_EXAM);

            sh->exam_index++;
            if (load_exam(sh, sh->exam_index)) {
                printf("TA 0: Loaded exam %d (student %d)\n",
                       sh->exam_index, sh->current_exam_student);
            } else {
                printf("TA 0: No more exams — setting student 9999.\n");
                sh->current_exam_student = 9999;
            }

            sem_signal(semid, SEM_EXAM);
        }

        sleep(1);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s NUM_TAS\n", argv[0]);
        exit(1);
    }

    int num_tas = atoi(argv[1]);
    if (num_tas < 2) {
        printf("Must use at least 2 TAs.\n");
        exit(1);
    }

    // Create shared memory
    int shmid = shmget(IPC_PRIVATE, sizeof(shared_data_t), IPC_CREAT | 0666);
    shared_data_t *sh = (shared_data_t*) shmat(shmid, NULL, 0);

    sh->exam_index = 1;
    load_rubric(sh);
    load_exam(sh, 1);

    printf("Main: Loaded first exam (student %d)\n",
           sh->current_exam_student);

    // Create semaphore set:
    // SEM_RUBRIC (1)
    // SEM_EXAM   (1)
    // SEM_Q0..Q4 (each = 1)
    int sem_count = 2 + NUM_QUESTIONS;
    int semid = semget(IPC_PRIVATE, sem_count, IPC_CREAT | 0666);

    for (int i = 0; i < sem_count; i++)
        semctl(semid, i, SETVAL, 1);

    // Fork TA processes
    for (int i = 0; i < num_tas; i++) {
        if (fork() == 0) {
            TA_process(i, sh, semid);
            exit(0);
        }
    }

    // Wait for all TAs
    for (int i = 0; i < num_tas; i++)
        wait(NULL);

    // Cleanup
    shmdt(sh);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);

    return 0;
}
