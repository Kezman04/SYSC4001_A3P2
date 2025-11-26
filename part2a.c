// Part 2(a) - SYSC4001 A3
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>

#define NUM_QUESTIONS 5
#define MAX_LINE 256

typedef struct {
    char rubric[NUM_QUESTIONS][MAX_LINE];
    int current_exam_student;
    int question_marked[NUM_QUESTIONS];
    int exam_index;
} shared_data_t;

void random_delay(double min, double max) {
    double duration = min + ((double)rand()/RAND_MAX)*(max-min);
    usleep(duration*1e6);
}

void load_rubric(shared_data_t *sh) {
    FILE *f = fopen("rubric.txt","r");
    if(!f){ perror("rubric.txt"); exit(1); }
    for(int i=0;i<NUM_QUESTIONS;i++){
        fgets(sh->rubric[i],MAX_LINE,f);
        sh->rubric[i][strcspn(sh->rubric[i],"\n")] = '\0';
    }
    fclose(f);
}

void save_rubric(shared_data_t *sh){
    FILE *f = fopen("rubric.txt","w");
    for(int i=0;i<NUM_QUESTIONS;i++)
        fprintf(f,"%s\n", sh->rubric[i]);
    fclose(f);
}

int load_exam(shared_data_t *sh,int num){
    char name[50];
    sprintf(name,"exam%d.txt",num);
    FILE *f = fopen(name,"r");
    if(!f) return 0;
    char buf[128];
    fgets(buf,128,f);
    sh->current_exam_student = atoi(buf);
    for(int i=0;i<NUM_QUESTIONS;i++) sh->question_marked[i]=0;
    fclose(f);
    return 1;
}

void TA_process(shared_data_t *sh,int id){
    srand(time(NULL)^getpid());
    while(1){
        int student = sh->current_exam_student;
        if(student==9999){
            printf("TA %d: stop\n",id);
            exit(0);
        }
        printf("TA %d reviewing rubric\n",id);
        for(int i=0;i<NUM_QUESTIONS;i++){
            random_delay(0.5,1.0);
            if(rand()%2){
                char *c=strchr(sh->rubric[i],',');
                if(c && c[2]) c[2] = c[2]+1;
                save_rubric(sh);
            }
        }
        for(int q=0;q<NUM_QUESTIONS;q++){
            if(!sh->question_marked[q]){
                sh->question_marked[q]=1;
                printf("TA %d marking Q%d student %d\n",id,q+1,student);
                random_delay(1.0,2.0);
            }
        }
        if(id==0){
            sh->exam_index++;
            if(!load_exam(sh,sh->exam_index)) sh->current_exam_student=9999;
        }
        sleep(1);
    }
}

int main(int argc,char *argv[]){
    if(argc<2){ printf("Usage: ./part2a N\n"); exit(1); }
    int n=atoi(argv[1]);
    int shmid=shmget(IPC_PRIVATE,sizeof(shared_data_t),0666|IPC_CREAT);
    shared_data_t *sh=shmat(shmid,NULL,0);
    sh->exam_index=1;
    load_rubric(sh);
    load_exam(sh,1);
    for(int i=0;i<n;i++){
        if(fork()==0) TA_process(sh,i);
    }
    while(wait(NULL)>0);
    return 0;
}
