// Part 2(b) - SYSC4001 A3 with Semaphores
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
#define SEM_EXAM 1
#define SEM_QBASE 2

typedef struct {
    char rubric[NUM_QUESTIONS][MAX_LINE];
    int current_exam_student;
    int question_marked[NUM_QUESTIONS];
    int exam_index;
} shared_data_t;

void sem_wait(int id,int n){
    struct sembuf op={n,-1,0};
    semop(id,&op,1);
}
void sem_signal(int id,int n){
    struct sembuf op={n,+1,0};
    semop(id,&op,1);
}

void random_delay(double a,double b){
    double s=a+((double)rand()/RAND_MAX)*(b-a);
    usleep(s*1e6);
}

void load_rubric(shared_data_t *sh){
    FILE *f=fopen("rubric.txt","r");
    if(!f){ perror("rubric"); exit(1);}
    for(int i=0;i<NUM_QUESTIONS;i++){
        fgets(sh->rubric[i],MAX_LINE,f);
        sh->rubric[i][strcspn(sh->rubric[i],"\n")]='\0';
    }
    fclose(f);
}

void save_rubric(shared_data_t *sh){
    FILE *f=fopen("rubric.txt","w");
    for(int i=0;i<NUM_QUESTIONS;i++)
        fprintf(f,"%s\n",sh->rubric[i]);
    fclose(f);
}

int load_exam(shared_data_t *sh,int idx){
    char name[50];
    sprintf(name,"exam%d.txt",idx);
    FILE *f=fopen(name,"r");
    if(!f) return 0;
    char buf[128];
    fgets(buf,128,f);
    sh->current_exam_student=atoi(buf);
    for(int i=0;i<NUM_QUESTIONS;i++) sh->question_marked[i]=0;
    fclose(f);
    return 1;
}

void TA_process(int id,shared_data_t *sh,int semid){
    srand(time(NULL)^getpid());
    while(1){
        int st=sh->current_exam_student;
        if(st==9999){
            printf("TA %d: stopping\n",id);
            exit(0);
        }
        printf("TA %d reviewing rubric\n",id);

        sem_wait(semid,SEM_RUBRIC);
        for(int i=0;i<NUM_QUESTIONS;i++){
            random_delay(0.5,1.0);
            if(rand()%2){
                char *c=strchr(sh->rubric[i],',');
                if(c && c[2]) c[2]++;
            }
        }
        save_rubric(sh);
        sem_signal(semid,SEM_RUBRIC);

        for(int q=0;q<NUM_QUESTIONS;q++){
            sem_wait(semid,SEM_QBASE+q);
            if(!sh->question_marked[q]){
                sh->question_marked[q]=1;
                printf("TA %d marking Q%d for %d\n",id,q+1,st);
                random_delay(1.0,2.0);
            }
            sem_signal(semid,SEM_QBASE+q);
        }

        if(id==0){
            sem_wait(semid,SEM_EXAM);
            sh->exam_index++;
            if(!load_exam(sh,sh->exam_index))
                sh->current_exam_student=9999;
            sem_signal(semid,SEM_EXAM);
        }
        sleep(1);
    }
}

int main(int argc,char *argv[]){
    if(argc<2) exit(1);
    int n=atoi(argv[1]);

    int shmid=shmget(IPC_PRIVATE,sizeof(shared_data_t),0666|IPC_CREAT);
    shared_data_t *sh=shmat(shmid,NULL,0);

    load_rubric(sh);
    sh->exam_index=1;
    load_exam(sh,1);

    int semid=semget(IPC_PRIVATE,2+NUM_QUESTIONS,0666|IPC_CREAT);
    for(int i=0;i<2+NUM_QUESTIONS;i++)
        semctl(semid,i,SETVAL,1);

    for(int i=0;i<n;i++)
        if(fork()==0) TA_process(i,sh,semid);

    while(wait(NULL)>0);
    return 0;
}
