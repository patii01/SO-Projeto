// Duarte Emanuel Ramos Meneses, 2019216949
// Patricia Beatriz Silva Costa, 2019213995

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <string.h>
#include <sys/types.h>
#include <wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/msg.h>

#define MAXLINHA 500
#define PIPE_NAME "PIPE"

// Estrutura com as configurações da corrida
typedef struct{
    int uni_tempo_pseg;
    int dist_volta;
    int n_voltas_cor;
    int num_equipas;
    int max_carros_pequi;
    int t_avaria;
    int t_box_min;
    int t_box_max;
    int cap_combus;
} config;

// Estrutura com as informações de cada carro
typedef struct{
    pthread_t n_thread;
    int num_carro;
    float velocidade;
    float consumo;
    int reabilidade;
    int estado;
    float combustivel;
    int num_paragens_box;
    int voltas;
    char nome_eq[10];
    int quer_box;
    int id_box;
    int dist_percorrida;
    int tem_avaria;
    int dist_total;

    int tempo_total;

} carro;

// Estrutura com as informações de cada box
typedef struct{
    int estado;
    pid_t pr_equipa;
    char nome[10];
    int quant_carros;
    int carro_analisar;
    int pos_seg;

    pthread_cond_t estado_box;
    pthread_mutex_t mutex_estado_box;

    pthread_cond_t carro_a_sair;
    pthread_mutex_t mutex_carro_a_sair;

} box;

// Estrutura com o que estará na memória partilhada
typedef struct{
    carro carros[1000];
    box boxes[100];
    carro estatisticas[1000];
    int quant_equipas;
    int quant_carros;
    int estado_corrida;
    int estado_corrida2;
    int vencedor;

    int terminados;

    int quant_top;

    int total_abastecimentos;
    int total_avarias;

    int flag_acabou;

    pthread_mutex_t mutex_estado_preparacao;
	pthread_cond_t estado_preparacao;
    pthread_mutex_t mutex_estado_comecar;
    pthread_cond_t estado_comecar;

    pthread_mutex_t mutex_estado_acabar;
    pthread_cond_t estado_acabar;

    pid_t processes_id[105];
    int c;

} mem_par;

// Estrutura com o que se lê do pipe
typedef struct{
    char equipa[10];
    int n_carro;
    int velocidade;
    float consumo;
    int reabilidade;
} le_pipe;

// Estrutura da mensagem da message queue
typedef struct{
    long mtype;
    int msg;
} msg_msgq;

config infos;
mem_par *shm;
int shmid;
sem_t *mutex, *sem_shm, *sem_inicio;
pid_t pr_racemanager, pr_avarias;
int pos = 0;    // Primeiro indice disponível no array de carros
int ids_threads[10];    // Array que, neste momento, guarda o número dos carros (threads)
time_t rawtime;
FILE *f_log = NULL;
int fd_named_pipe;
int fd_unnamed_pipes [1000][2];
int mqid;

pthread_mutexattr_t mutex_estado_preparacao;
pthread_condattr_t estado_preparacao;
pthread_mutexattr_t mutex_estado_comecar;
pthread_condattr_t estado_comecar;
pthread_mutexattr_t mutex_estado_acabar;
pthread_condattr_t estado_acabar;

pthread_condattr_t estado_box[100];
pthread_mutexattr_t mutex_estado_box[100];

void escrita_ficheiro(char *msg){
    fflush(stdout);
    fflush(f_log);
    sem_wait(mutex);
    fprintf(stderr, "%s", msg);
    fprintf(f_log,  "%s", msg);
    sem_post(mutex);
}

// Função responsável por fechar todos os recursos utilizados (semáforos, memória partilhada)
void finish(){
    char msg[MAXLINHA] = "";
    time(&rawtime);
    snprintf(msg, MAXLINHA, "\n%02d:%02d:%02d SIMULATOR CLOSING!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
    escrita_ficheiro(msg);
    
    sem_wait(sem_shm);
    for (int i = 0; i< shm->c; i++){
        kill(shm->processes_id[i], SIGKILL);
    }
    sem_post(sem_shm);

    kill(pr_racemanager, SIGKILL);
    kill(pr_avarias, SIGKILL);

    pthread_cond_destroy(&shm->estado_preparacao);
    pthread_mutex_destroy(&shm->mutex_estado_preparacao);
    pthread_cond_destroy(&shm->estado_acabar);
    pthread_mutex_destroy(&shm->mutex_estado_acabar);
    pthread_cond_destroy(&shm->estado_comecar);
    pthread_mutex_destroy(&shm->mutex_estado_comecar);
    for (int i=0; i<100; i++) {
        pthread_cond_destroy(&shm->boxes[i].estado_box);
        pthread_mutex_destroy(&shm->boxes[i].mutex_estado_box);
    }

    sem_close(mutex);
    sem_unlink("MUTEX");
    sem_close(sem_shm);
    sem_unlink("SEM_SHM");
    sem_close(sem_inicio);
    sem_unlink("SEM_INICIO");
    close(fd_named_pipe);
    unlink("PIPE");
    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);
    fclose(f_log);
    exit(0);
}

void estatisticas() {
  
    printf("\nSTATISTICS:\n");
    printf("\n\tTOP 5:\n");

    sem_wait(sem_shm);

    for (int i = 0; i < shm->quant_carros; i++){
        for (int j= 0; j < shm->quant_carros; j++) {
            if (shm->carros[i].num_carro == shm->estatisticas[j].num_carro){
                shm->estatisticas[j] = shm->carros[i];
            }
        }
    }

    for (int i = 0; i < shm->quant_carros; i++){
        for (int j = i + 1; j < shm->quant_carros; j++){
            if ((shm->estatisticas[i].dist_total > shm->estatisticas[j].dist_total) && (shm->estatisticas[i].tempo_total <= shm->estatisticas[j].tempo_total)){
                carro a =  shm->estatisticas[i];
                shm->estatisticas[i] = shm->estatisticas[j];
                shm->estatisticas[j] = a;
            }
        }
    }                        
        
    pos = 0;

    for (int i= (shm->quant_carros) - 1; pos < 5; i--){
        printf("\t%dº - CAR: %d, TEAM: %s, LAPS: %d, BOX STOPS: %d\n", (pos+1), shm->estatisticas[i].num_carro, shm->estatisticas[i].nome_eq, shm->estatisticas[i].voltas, shm->estatisticas[i].num_paragens_box);
        pos ++;
    }

    printf("\n\tLAST PLACE: CAR: %d, TEAM: %s, LAPS: %d, BOX STOPS: %d\n",shm->estatisticas[0].num_carro, shm->estatisticas[0].nome_eq, shm->estatisticas[0].voltas, shm->estatisticas[0].num_paragens_box);

    printf("\n\tTOTAL BREAKDOWN: %d\n", shm->total_avarias);

    printf("\n\tTOTAL SUPPLIES: %d\n", shm->total_abastecimentos);

    printf("\n\tNUMBER OF CARS ON THE TRACK: %d\n\n", (shm->quant_carros - shm->terminados));
    
    sem_post(sem_shm);
    
}

void sigint(int signum) {

    char msg[MAXLINHA];
    time(&rawtime);
    snprintf(msg, MAXLINHA, "\n%02d:%02d:%02d ^C PRESSED\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
    escrita_ficheiro(msg);

    pthread_mutex_lock(&shm->mutex_estado_acabar);
    while (shm->estado_corrida != 3) {
        pthread_cond_wait(&shm->estado_acabar, &shm->mutex_estado_acabar);
    }
    pthread_mutex_unlock(&shm->mutex_estado_acabar);

    estatisticas();
    finish();
}

void sigtstp(int signum){
    char msg[MAXLINHA];
    time(&rawtime);
    snprintf(msg, MAXLINHA, "\n%02d:%02d:%02d ^Z PRESSED\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
    escrita_ficheiro(msg);
    estatisticas();
}

void sigurs1(int signum) {
    char msg[MAXLINHA];
    time(&rawtime);
    snprintf(msg, MAXLINHA, "\n%02d:%02d:%02d SIGNAL USR1 RECEIVED\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
    escrita_ficheiro(msg);

    pthread_mutex_lock(&shm->mutex_estado_acabar);
    while (shm->estado_corrida != 3) {
        pthread_cond_wait(&shm->estado_acabar, &shm->mutex_estado_acabar);
    }
    pthread_mutex_unlock(&shm->mutex_estado_acabar);

    estatisticas();
    /*
    snprintf(msg, MAXLINHA, "START RACE!");
    write(fd_named_pipe, "START RACE!", sizeof(char));
    */
}

void remove_chr(char *str,char c){
    char *pr = str, *pw = str;
    while (*pr) {
        *pw = *pr++;
        pw += (*pw != c);
    }
    *pw = '\0';

}

int e_numero(char *s) {
    int n_ponto = 0;
    for (int i=0; i<strlen(s); i++){
        if (((int)s[i]>=48 && (int)s[i]<=57) || ((int)s[i]==46 && n_ponto == 0)){
            if ((int)s[i]==46)
                n_ponto++;
            continue;
        }
        else{
            return 1;
        }
    }
    return 0;
}

int e_numero2(char *num) {
    if (num != NULL) {
        for (int i = 0; i < strlen(num) - 2; i++){  // Últimos 2 caracteres não interessam
            if ((int)num[i] < 48 || (int)num[i] > 57)   // Ascii do 0 e do 9
                return 1;
        }
        return 0;
    }
    return 1;
}

int e_numero3(char *num) {
    if (num != NULL) {
        for (int i = 0; i < strlen(num) - 1; i++){
            if ((int)num[i] < 48 || (int)num[i] > 57)   // Ascii do 0 e do 9
                return 1;
        }
        return 0;
    }
    return 1;
}

le_pipe verifica_comando_pipe(char *str) {
    le_pipe res = {"", 0,0,0,-1};
    char *token;
    char msg[MAXLINHA] = "";
    int pos = 0;
    char command[100];
    strcpy(command, str);

    token = strtok(str, " ");

    while (token != NULL){
        if (pos == 0){
            if (strcmp(token, "ADDCAR") != 0) {
                time(&rawtime);
                snprintf(msg, MAXLINHA, "%02d:%02d:%02d WRONG COMMAND => %s\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, command);
                escrita_ficheiro(msg);
                break;
            }
        }
        else if (pos == 1){
            if (strcmp(token, "TEAM:") != 0) {
                time(&rawtime);
                snprintf(msg, MAXLINHA, "%02d:%02d:%02d WRONG COMMAND => %s\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, command);
                escrita_ficheiro(msg);
                break;
            }
        }
        else if (pos == 2) {
            strcpy(res.equipa, token);
        }

        else if (pos == 3){
            if (strcmp(token, "CAR:") != 0) {
                time(&rawtime);
                snprintf(msg, MAXLINHA, "%02d:%02d:%02d WRONG COMMAND => %s\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, command);
                escrita_ficheiro(msg);
                break;
            }
        }
        else if (pos == 4){
            if (e_numero3(token) != 0) {
                time(&rawtime);
                snprintf(msg, MAXLINHA, "%02d:%02d:%02d WRONG COMMAND => %s\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, command);
                escrita_ficheiro(msg);
                break;
            }
            else {
                res.n_carro = atoi(token);
                int flag = 0;
                for (int i=0; i<shm->quant_carros; i++){
                    if (shm->carros[i].num_carro == res.n_carro){
                        flag = 1;
                        break;
                    }

                }
                if (flag == 1) {
                    time(&rawtime);
                    snprintf(msg, MAXLINHA, "%02d:%02d:%02d WRONG COMMAND => %s\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, command);
                    escrita_ficheiro(msg);
                    break;
                }     
            } 
        }
        else if (pos == 5){
            if (strcmp(token, "SPEED:") != 0)  {
                time(&rawtime);
                snprintf(msg, MAXLINHA, "%02d:%02d:%02d WRONG COMMAND => %s\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, command);
                escrita_ficheiro(msg);
                break;
            }
        }
        else if (pos == 6){
            if (e_numero3(token) != 0) {
                time(&rawtime);
                snprintf(msg, MAXLINHA, "%02d:%02d:%02d WRONG COMMAND => %s\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, command);
                escrita_ficheiro(msg);
                break;
            }
            else res.velocidade = atoi(token);
        }
        else if (pos == 7){
            if (strcmp(token, "CONSUMPTION:") != 0)  {
                time(&rawtime);
                snprintf(msg, MAXLINHA, "%02d:%02d:%02d WRONG COMMAND => %s\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, command);
                escrita_ficheiro(msg);
                break;
            };
        }
        else if (pos == 8){
            if (e_numero(token) != 0) {
                time(&rawtime);
                snprintf(msg, MAXLINHA, "%02d:%02d:%02d WRONG COMMAND => %s\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, command);
                escrita_ficheiro(msg);
                break;
            }
            else res.consumo = atof(token);
        }
        else if (pos == 9){
            if (strcmp(token, "RELIABILITY:") != 0)  {
                time(&rawtime);
                snprintf(msg, MAXLINHA, "%02d:%02d:%02d WRONG COMMAND => %s\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, command);
                escrita_ficheiro(msg);
                break;
            }
        }
        else if (pos == 10){
            if (e_numero3(token) != 0) {
                time(&rawtime);
                snprintf(msg, MAXLINHA, "%02d:%02d:%02d WRONG COMMAND => %s\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, command);
                escrita_ficheiro(msg);
                break;}
            else {
                res.reabilidade = atoi(token);
            }
        }

        pos ++;
        token = strtok(NULL, " ");
    }

    return res;
}


// Função que faz a leitura do ficheiro de configurações e, caso as informações sejam válidas, as coloca na estrutura correspondente
void readfile(char *filename, FILE *f_log){
    time(&rawtime);
    FILE *fich_config = NULL;
    fich_config = fopen(filename, "r");
    char msg[MAXLINHA] = "";
    if (fich_config == NULL){
        snprintf(msg, MAXLINHA, "%02d:%02d:%02d Ficheiro de leitura das configurações não existe.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        escrita_ficheiro(msg);
        exit(1);
    }

    char linha[MAXLINHA + 1];

    fgets(linha, MAXLINHA, fich_config);

    if (e_numero2(linha) == 0){
        infos.uni_tempo_pseg = atoi(linha);
        fgets(linha, MAXLINHA, fich_config);
        char *x;
        char *z;
        x = strtok(linha, ", ");
        z = strtok(NULL, ", ");
        if (e_numero2(x) == 0) {
            infos.dist_volta = atoi(x);
            if (e_numero2(z) == 0) {
                infos.n_voltas_cor = atoi(z);
                fgets(linha, MAXLINHA, fich_config);
                if (e_numero2(linha) == 0) {
                    if (atoi(linha) > 2){
                        if (atoi(linha) < 101){
                            infos.num_equipas = atoi(linha);
                            fgets(linha, MAXLINHA, fich_config);
                            if (e_numero2(linha) == 0) {
                                if (atoi(linha)*infos.num_equipas < 1001) {
                                    infos.max_carros_pequi = atoi(linha);
                                    fgets(linha, MAXLINHA, fich_config);
                                    if (e_numero2(linha) == 0) {
                                        infos.t_avaria = atoi(linha);
                                        fgets(linha, MAXLINHA, fich_config);
                                        x = strtok(linha, ", ");
                                        z = strtok(NULL, ", ");
                                        if (e_numero2(x) == 0){
                                            infos.t_box_min = atoi(x);
                                            if (e_numero2(z) == 0){
                                                infos.t_box_max = atoi(z);
                                                fgets(linha, MAXLINHA, fich_config);
                                                if (e_numero2(linha) == 0){
                                                    infos.cap_combus = atoi(linha);
                                                }
                                                else {
                                                    time(&rawtime);
                                                    snprintf(msg, MAXLINHA, "%02d:%02d:%02d Valor da capacidade do depósito de combustível inválido! Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                                                    escrita_ficheiro(msg);
                                                    exit(1);
                                                }
                                            }
                                            else {
                                                time(&rawtime);
                                                snprintf(msg, MAXLINHA, "%02d:%02d:%02d Valor do tempo máximo de reparação inválido! Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                                                escrita_ficheiro(msg);
                                                exit(1);
                                            }
                                        }
                                        else{
                                            time(&rawtime);
                                            snprintf(msg, MAXLINHA, "%02d:%02d:%02d Valor do tempo mínimo de reparação inválido! Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min,localtime(&rawtime)->tm_sec);
                                            escrita_ficheiro(msg);
                                            exit(1);
                                        }
                                    }
                                    else{
                                        time(&rawtime);
                                        snprintf(msg, MAXLINHA, "%02d:%02d:%02d Valor do número de unidades de tempo entre novo cálculo de avaria inválido! Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                                        escrita_ficheiro(msg);
                                        exit(1);
                                    }
                                }
                                else{
                                    time(&rawtime);
                                    snprintf(msg, MAXLINHA, "%02d:%02d:%02d Cuidado! Máximo de carros é 1000. Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                                    escrita_ficheiro(msg);
                                    exit(1);
                                }
                            }
                            else{
                                time(&rawtime);
                                snprintf(msg, MAXLINHA, "%02d:%02d:%02d Valor do número máximo de carros por equipa inválido! Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                                escrita_ficheiro(msg);
                                exit(1);
                            }
                        }
                        else{
                            time(&rawtime);
                            snprintf(msg, MAXLINHA, "%02d:%02d:%02d Máximo de equipas suportado é 100! Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                            escrita_ficheiro(msg);
                            exit(1);
                        }
                    }
                    else{
                        time(&rawtime);
                        snprintf(msg, MAXLINHA, "%02d:%02d:%02d Têm de existir pelo menos 2 equipas! Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                        escrita_ficheiro(msg);
                        exit(1);
                    }

                }
                else{
                    time(&rawtime);
                    snprintf(msg, MAXLINHA, "%02d:%02d:%02d Valor do número de equipas inválido! Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                    escrita_ficheiro(msg);
                    exit(1);
                }
            }
            else{
                time(&rawtime);
                snprintf(msg, MAXLINHA, "%02d:%02d:%02d Valor do número de voltas da corrida inválido! Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                escrita_ficheiro(msg);
                exit(1);
            }
        }
        else{
            time(&rawtime);
            snprintf(msg, MAXLINHA, "%02d:%02d:%02d Valor da distância de cada volta inválido! Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
            escrita_ficheiro(msg);
            exit(1);
        }
    }
    else{
        time(&rawtime);
        snprintf(msg, MAXLINHA, "%02d:%02d:%02d Valor do número de unidades de tempo por segundo inválido! Programa abortado.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        escrita_ficheiro(msg);
        exit(1);
    }

    fclose(fich_config);
}


void gestor_avarias(){
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    msg_msgq my_msg;
    my_msg.msg = 1;

    while (1) {

        pthread_mutex_lock(&shm->mutex_estado_comecar);
        while (shm->estado_corrida != 2){
            pthread_cond_wait(&shm->estado_comecar, &shm->mutex_estado_comecar);
        }
        pthread_mutex_unlock(&shm->mutex_estado_comecar);

        if (shm->estado_corrida != 0){
            for (int i=0; i<shm->quant_carros; i++){
                int num = rand() % 100;
                if ((num > shm->carros[i].reabilidade) && (shm->carros[i].estado != 3) && (shm->carros[i].tem_avaria == 0) && (shm->carros[i].estado != 5) && (shm->carros[i].estado != 4)) {
                    my_msg.mtype = (i+1);
                    msgsnd(mqid, &my_msg, sizeof(msg_msgq)-sizeof(long), 0);
                }
            }
        }
        sleep(infos.t_avaria);
    }
}

// Função responsável por gerir as coisas relativas aos carros (threads)
void *f_car(void *thread_id) {

    msg_msgq my_msg;
    int id = *((int *)thread_id);   // Posição do carro no array de carros
    char msg[MAXLINHA];


    pthread_mutex_lock(&shm->mutex_estado_comecar);
    while (shm->estado_corrida != 2){
        pthread_cond_wait(&shm->estado_comecar, &shm->mutex_estado_comecar);
    }
    pthread_mutex_unlock(&shm->mutex_estado_comecar);

    sem_wait(sem_shm);
    shm->carros[id].estado = 1; // carro em corrida
    sem_post(sem_shm);

    while (shm->carros[id].voltas < infos.n_voltas_cor) {

        time(&rawtime);
        if (msgrcv(mqid, &my_msg, sizeof(msg_msgq)-sizeof(long), (id+1), IPC_NOWAIT) > 0) {
            snprintf(msg, MAXLINHA, "%02d:%02d:%02d NEW PROBLEM IN CAR %d\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, shm->carros[id].num_carro);
            escrita_ficheiro(msg);
            if (shm->carros[id].estado != 2) {
                sem_wait(sem_shm);
                (shm->total_avarias) ++;
                shm->carros[id].estado = 2;
                shm->carros[id].tem_avaria = 1;
                (shm->boxes[shm->carros[id].id_box].pos_seg) ++;
                sem_post(sem_shm);
            }
            // UNNAMED PIPE
            //write(fd_unnamed_pipes[id][1], msg, sizeof(msg));
            
          
            if(shm->boxes[shm->carros[id].id_box].estado == 0) {
                sem_wait(sem_shm);
                shm->boxes[shm->carros[id].id_box].estado = 1; 
                sem_post(sem_shm);
            }
    
        }

        else if ((shm->carros[id].combustivel <= (infos.dist_volta / shm->carros[id].velocidade) * 4 * shm->carros[id].consumo) && (shm->carros[id].quer_box != 1)) {
            sem_wait(sem_shm);
            shm->carros[id].quer_box = 1;
            (shm->boxes[shm->carros[id].id_box].pos_seg) ++;
            sem_post(sem_shm);

            if(shm->boxes[shm->carros[id].id_box].estado == 0) {
                sem_wait(sem_shm);
                shm->boxes[shm->carros[id].id_box].estado = 1; 
                sem_post(sem_shm);
            }     
        }

        else if ((shm->carros[id].combustivel <= (infos.dist_volta / shm->carros[id].velocidade) * 2 * shm->carros[id].consumo) && (shm->carros[id].estado != 2)) {
            time(&rawtime);
            sem_wait(sem_shm);
            shm->carros[id].estado = 2;
            (shm->boxes[shm->carros[id].id_box].pos_seg) ++;
            sem_post(sem_shm);

            snprintf(msg, MAXLINHA, "%02d:%02d:%02d CHANGE OF STATE => CAR %d IN SAFETY MODE\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, shm->carros[id].num_carro);
            escrita_ficheiro(msg);

            //write(fd_unnamed_pipes[id][1], msg, sizeof(msg));

            if(shm->boxes[shm->carros[id].id_box].estado == 0) {
                sem_wait(sem_shm);
                shm->boxes[shm->carros[id].id_box].estado = 1; 
                sem_post(sem_shm);
            }
        }

        else if (shm->carros[id].combustivel == 0) {
            time(&rawtime);
            sem_wait(sem_shm);
            shm->carros[id].estado = 4;
            (shm->terminados) ++;
            sem_post(sem_shm);

            snprintf(msg, MAXLINHA, "%02d:%02d:%02d CHANGE OF STATE => CAR %d IN WITHDRAWAL MODE\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, shm->carros[id].num_carro);
            escrita_ficheiro(msg);

            //write(fd_unnamed_pipes[id][1], msg, sizeof(msg));

            break;
        }

        if (shm->carros[id].estado == 1) {
            sem_wait(sem_shm);
            shm->carros[id].combustivel = shm->carros[id].combustivel - shm->carros[id].consumo;
            shm->carros[id].dist_percorrida = shm->carros[id].dist_percorrida + shm->carros[id].velocidade;
            shm->carros[id].dist_total = shm->carros[id].dist_total + shm->carros[id].velocidade;
            sem_post(sem_shm); 
        }

        else if (shm->carros[id].estado == 2) {
            sem_wait(sem_shm);
            shm->carros[id].combustivel = shm->carros[id].combustivel - (0.4*(shm->carros[id].consumo));
            shm->carros[id].dist_percorrida = shm->carros[id].dist_percorrida + (0.3 * shm->carros[id].velocidade);
            shm->carros[id].dist_total = shm->carros[id].dist_total + (0.3 * shm->carros[id].velocidade);
            sem_post(sem_shm); 
        }


        if ((((int)shm->carros[id].dist_total / infos.dist_volta) != shm->carros[id].voltas) && (shm->carros[id].voltas + 1 == infos.n_voltas_cor)) {
            sem_wait(sem_shm);
            (shm->carros[id].voltas) ++;

            shm->carros[id].dist_total = (shm->carros[id].voltas) * infos.dist_volta;
            
            shm->carros[id].estado = 5;
            (shm->terminados) ++;
            if (shm->vencedor == -1)
                shm->vencedor = id;
            sem_post(sem_shm);

            time(&rawtime);
            snprintf(msg, MAXLINHA, "%02d:%02d:%02d CHANGE OF STATE => CAR %d FINISHED THE RACE\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, shm->carros[id].num_carro);
            escrita_ficheiro(msg);

            //write(fd_unnamed_pipes[id][1], msg, sizeof(msg));
        }

        
        else if ((((int)shm->carros[id].dist_total / infos.dist_volta) != shm->carros[id].voltas) && (shm->carros[id].voltas + 1 != infos.n_voltas_cor)){ 
            sem_wait(sem_shm);
            (shm->carros[id].voltas) ++;

            shm->carros[id].dist_percorrida = shm->carros[id].dist_percorrida - infos.dist_volta;
            
            shm->carros[id].dist_total = (shm->carros[id].voltas) * infos.dist_volta;

            sem_post(sem_shm);

            if (shm->carros[id].estado == 2) {
                if (shm->boxes[shm->carros[id].id_box].estado != 2) {
                    sem_wait(sem_shm);
                    shm->boxes[shm->carros[id].id_box].carro_analisar = id;
                    sem_post(sem_shm);

                    pthread_mutex_lock(&shm->boxes[shm->carros[id].id_box].mutex_estado_box);
                    pthread_cond_broadcast(&shm->boxes[shm->carros[id].id_box].estado_box);
                    pthread_mutex_unlock(&shm->boxes[shm->carros[id].id_box].mutex_estado_box);

                    pthread_mutex_lock(&shm->boxes[shm->carros[id].id_box].mutex_carro_a_sair);
                    while (shm->boxes[shm->carros[id].id_box].carro_analisar != -1) {
                        pthread_cond_wait(&shm->boxes[shm->carros[id].id_box].carro_a_sair, &shm->boxes[shm->carros[id].id_box].mutex_carro_a_sair);
                    }
                    pthread_mutex_unlock(&shm->boxes[shm->carros[id].id_box].mutex_carro_a_sair);

                }
            }
            else if (shm->carros[id].quer_box == 1) {
                if (shm->boxes[shm->carros[id].id_box].estado == 0) {
                    sem_wait(sem_shm);
                    shm->boxes[shm->carros[id].id_box].carro_analisar = id;
                    sem_post(sem_shm);

                    pthread_mutex_lock(&shm->boxes[shm->carros[id].id_box].mutex_estado_box);
                    pthread_cond_broadcast(&shm->boxes[shm->carros[id].id_box].estado_box);
                    pthread_mutex_unlock(&shm->boxes[shm->carros[id].id_box].mutex_estado_box);

                    pthread_mutex_lock(&shm->boxes[shm->carros[id].id_box].mutex_carro_a_sair);
                    while (shm->boxes[shm->carros[id].id_box].carro_analisar != -1) {
                        pthread_cond_wait(&shm->boxes[shm->carros[id].id_box].carro_a_sair, &shm->boxes[shm->carros[id].id_box].mutex_carro_a_sair);
                    }
                    pthread_mutex_unlock(&shm->boxes[shm->carros[id].id_box].mutex_carro_a_sair);

                }
            }
        }

        sem_wait(sem_shm);
        shm->carros[id].tempo_total = shm->carros[id].tempo_total + infos.uni_tempo_pseg;
        sem_post(sem_shm);
        
        sleep(infos.uni_tempo_pseg);
    }

    pthread_exit(NULL);
}

// Função responsável pelo gestor de equipa
void team_manager(int pos) {
    char msg[MAXLINHA];
    int ids[100];
    int contador = 0;

    signal(SIGUSR1, SIG_IGN);

    pthread_mutex_lock(&shm->mutex_estado_preparacao);
    while (shm->estado_corrida != 1) {
        pthread_cond_wait(&shm->estado_preparacao, &shm->mutex_estado_preparacao);
    }
    pthread_mutex_unlock(&shm->mutex_estado_preparacao);


    for (int i=0; i<shm->quant_carros; i++){
        if (strcmp(shm->carros[i].nome_eq, shm->boxes[pos].nome) == 0){
            sem_post(sem_inicio);
            ids[contador] = i;
            pthread_create(&(shm->carros[i].n_thread), NULL, f_car, &ids[contador++]); // Cria os carros
        }
    }

    while (shm->terminados != shm->quant_carros) {
        pthread_mutex_lock(&shm->boxes[pos].mutex_estado_box);
        while (shm->boxes[pos].carro_analisar == -1) {
            pthread_cond_wait(&shm->boxes[pos].estado_box, &shm->boxes[pos].mutex_estado_box);
        }
        pthread_mutex_unlock(&shm->boxes[pos].mutex_estado_box);

        sem_wait(sem_shm);
        (shm->total_abastecimentos) ++;
        shm->boxes[pos].estado = 2;
        shm->carros[shm->boxes[pos].carro_analisar].estado = 3;
        (shm->carros[shm->boxes[pos].carro_analisar].num_paragens_box) ++;
        sem_post(sem_shm);

        time(&rawtime);
        snprintf(msg, MAXLINHA, "%02d:%02d:%02d CHANGE OF STATE => CAR %d IN BOX\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, shm->carros[shm->boxes[pos].carro_analisar].num_carro);
        escrita_ficheiro(msg);

        //write(fd_unnamed_pipes[shm->boxes[pos].carro_analisar][1], msg, sizeof(msg));

        
        if (shm->carros[shm->boxes[pos].carro_analisar].quer_box != 1){

            int tempo = rand() % (infos.t_box_max - infos.t_box_min + 1) + infos.t_box_min;
            sleep(tempo);

            sem_wait(sem_shm);
            shm->carros[shm->boxes[pos].carro_analisar].combustivel = infos.cap_combus;
            shm->boxes[pos].pos_seg = shm->boxes[pos].pos_seg - 1;
            shm->carros[shm->boxes[pos].carro_analisar].tem_avaria = 0;
            sem_post(sem_shm);

            sleep(2);

            sem_wait(sem_shm);
            if (shm->boxes[pos].pos_seg == 0){
                shm->boxes[pos].estado = 0;
            }
            else{
                shm->boxes[pos].estado = 1;
            }
            shm->carros[shm->boxes[pos].carro_analisar].estado = 1;
            sem_post(sem_shm);
            time(&rawtime);
            snprintf(msg, MAXLINHA, "%02d:%02d:%02d CHANGE OF STATE => CAR %d IN RACE MODE\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, shm->carros[shm->boxes[pos].carro_analisar].num_carro);
            escrita_ficheiro(msg);

            //write(fd_unnamed_pipes[shm->boxes[pos].carro_analisar][1], msg, sizeof(msg));

            sem_wait(sem_shm);
            shm->boxes[pos].carro_analisar = -1;
      
            shm->carros[shm->boxes[pos].carro_analisar].tempo_total = shm->carros[shm->boxes[pos].carro_analisar].tempo_total + 2 + tempo;
            sem_post(sem_shm);
        }
        else{
            sem_wait(sem_shm);
            shm->carros[shm->boxes[pos].carro_analisar].combustivel = infos.cap_combus;
            shm->carros[shm->boxes[pos].carro_analisar].quer_box = 0;
            shm->carros[shm->boxes[pos].carro_analisar].tempo_total = shm->carros[shm->boxes[pos].carro_analisar].tempo_total + 2;
            sem_post(sem_shm);
            sleep(2);
        }


        pthread_mutex_lock(&shm->boxes[pos].mutex_carro_a_sair);
        pthread_cond_broadcast(&shm->boxes[pos].carro_a_sair);
        pthread_mutex_unlock(&shm->boxes[pos].mutex_carro_a_sair);


    }

    for (int i=0; i<shm->quant_carros; i++){
        if (strcmp(shm->carros[i].nome_eq, shm->boxes[pos].nome) == 0){
            pthread_join(shm->carros[i].n_thread, NULL); 
        }
    }
    
    snprintf(msg, MAXLINHA, "ACABA");
    if (shm->flag_acabou == 0){
        write(fd_named_pipe, msg, MAXLINHA);
    }

    

    
}

// Função responsável pelo gestor de corrida
void race_manager(){

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGUSR1, sigurs1);

    fd_set read_set;
    int estado_corrida = 0;
    char str[MAXLINHA];
    char msg[MAXLINHA];
    //char est[MAXLINHA];
    //int nread;

    //abrir named pipe para leitura
	if ((fd_named_pipe = open(PIPE_NAME, O_RDWR|O_NONBLOCK)) < 0) {
		perror("Cannot open pipe for reading");
		exit(1);
	}

    while (1){

        FD_ZERO(&read_set);
        FD_SET(fd_named_pipe, &read_set);
        if (select(fd_named_pipe + 1, &read_set, NULL, NULL, NULL) > 0){
            if (FD_ISSET(fd_named_pipe, &read_set)){
                int nread =  read(fd_named_pipe, (void *)&str, MAXLINHA);
                str[nread-1]='\0';
                if (estado_corrida == 0 || estado_corrida == 3) {
                    if (strcmp(str, "START RACE!") == 0){
                        snprintf(msg, MAXLINHA, "%02d:%02d:%02d NEW COMMAND RECEIVED => START RACE!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                        escrita_ficheiro(msg);
                        if (shm->quant_equipas != infos.num_equipas){
                            time(&rawtime);
                            snprintf(msg, MAXLINHA, "%02d:%02d:%02d CANNOT START, NOT ENOUGH TEAMS.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                            escrita_ficheiro(msg);
                        }
                        else {
                            time(&rawtime);
                            snprintf(msg, MAXLINHA, "%02d:%02d:%02d RACE STARTING!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                            escrita_ficheiro(msg);

                            pthread_mutex_lock(&shm->mutex_estado_preparacao);
                            shm->estado_corrida = 1;
                            estado_corrida = 1;
                            pthread_cond_broadcast(&shm->estado_preparacao);
                            pthread_mutex_unlock(&shm->mutex_estado_preparacao);

                            for (int i=0; i < shm->quant_carros; i++){
                                sem_wait(sem_inicio);
                            }

                            for (int i = 0; i < shm->quant_carros; i++){
                                shm->estatisticas[i] = shm->carros[i];                      
                            }

                            pthread_mutex_lock(&shm->mutex_estado_comecar);
                            estado_corrida = 2;
                            shm->estado_corrida = 2;
                            pthread_cond_broadcast(&shm->estado_comecar);
                            pthread_mutex_unlock(&shm->mutex_estado_comecar);

                        }
                    }
                    else {
                        remove_chr(str, ',');
                        le_pipe dados = verifica_comando_pipe(str);
                        if (dados.reabilidade != -1) {

                            if (shm->quant_equipas == 0) {
                                fflush(stdout);
                                fflush(f_log);
                                if (fork() == 0) {

                                    sem_wait(sem_shm);
                                    shm->processes_id[(shm->c)] = getpid();
                                    (shm->c)++;
                                    time(&rawtime);
                                    box equip;
                                    strcpy(equip.nome ,dados.equipa);
                                    equip.estado = 0;
                                    equip.pr_equipa = getpid();
                                    equip.quant_carros = 1;
                                    equip.pos_seg = 0;
                                    equip.carro_analisar = -1;

                                    pthread_cond_init(&equip.estado_box, NULL);
                                    pthread_mutex_init(&equip.mutex_estado_box, NULL);
                                    pthread_cond_init(&equip.carro_a_sair, NULL);
                                    pthread_mutex_init(&equip.mutex_carro_a_sair, NULL);

                                    shm->boxes[shm->quant_equipas] = equip;

                                    carro novo;
                                    novo.consumo = dados.consumo;
                                    novo.num_carro = dados.n_carro;
                                    novo.reabilidade = dados.reabilidade;
                                    novo.velocidade = dados.velocidade;
                                    strcpy(novo.nome_eq, dados.equipa);
                                    novo.combustivel = infos.cap_combus;
                                    novo.num_paragens_box = 0;
                                    novo.voltas = 0;
                                    novo.quer_box = 0;
                                    novo.id_box = shm->quant_equipas;
                                    novo.dist_percorrida = 0;
                                    novo.tem_avaria = 0;
                                    novo.dist_total = 0;
                                    novo.tempo_total = 0;
                                    shm->carros[(shm->quant_carros)] = novo;
                                    pipe(fd_unnamed_pipes[shm->quant_carros]);

                                    (shm->quant_carros)++;
                                    (shm->quant_equipas)++;

                                    sem_post(sem_shm);

                                    snprintf(msg, MAXLINHA, "%02d:%02d:%02d NEW CAR LOADED => TEAM: %s CAR: %d SPEED: %f CONSUMPTION: %f RELIABILITY: %d.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec,dados.equipa, novo.num_carro, novo.velocidade, novo.consumo,novo.reabilidade);
                                    escrita_ficheiro(msg);
                                    fflush(stdout);
                                    fflush(f_log);
                                    team_manager((shm->quant_equipas)-1);

                                    exit(0);
                                }
                            }
                            else{
                                for (int i = 0; i < shm->quant_equipas; i++) {
                                    if(strcmp(shm->boxes[i].nome, dados.equipa) == 0) {
                                        if (shm->boxes[i].quant_carros < infos.max_carros_pequi) {
                                            sem_wait(sem_shm);
                                            time(&rawtime);
                                            carro novo;
                                            novo.consumo = dados.consumo;
                                            novo.num_carro = dados.n_carro;
                                            novo.reabilidade = dados.reabilidade;
                                            novo.velocidade = dados.velocidade;
                                            strcpy(novo.nome_eq, dados.equipa);
                                            novo.combustivel = infos.cap_combus;
                                            novo.num_paragens_box = 0;
                                            novo.voltas = 0;
                                            novo.quer_box = 0;
                                            novo.id_box = i;
                                            novo.dist_percorrida = 0;
                                            novo.tem_avaria = 0;
                                            novo.dist_total = 0;
                                            novo.tempo_total = 0;
                                            shm->carros[shm->quant_carros] = novo;
                                            pipe(fd_unnamed_pipes[shm->quant_carros]);

                                            snprintf(msg, MAXLINHA, "%02d:%02d:%02d NEW CAR LOADED => TEAM: %s CAR: %d SPEED: %f CONSUMPTION: %f RELIABILITY: %d.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec,dados.equipa, novo.num_carro, novo.velocidade, novo.consumo,novo.reabilidade);
                                            escrita_ficheiro(msg);
                                            (shm->boxes[i].quant_carros)++;
                                            (shm->quant_carros)++;
                                            sem_post(sem_shm);
                                            break;
                                        }
                                        else {
                                            time(&rawtime);
                                            snprintf(msg, MAXLINHA, "%02d:%02d:%02d TEAM %s IS FULL => REFUSED CAR\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, dados.equipa);
                                            escrita_ficheiro(msg);
                                            break;
                                        }
                                    }
                                    else if ((i == (shm->quant_equipas)-1) && (strcmp(shm->boxes[i].nome, dados.equipa) != 0) && (shm->quant_equipas < infos.num_equipas)){
                                            fflush(stdout);
                                            fflush(f_log);
                                            if (fork() == 0){

                                                sem_wait(sem_shm);
                                                shm->processes_id[(shm->c)] = getpid();
                                                (shm->c)++;
                                                time(&rawtime);
                                                box equip;
                                                strcpy(equip.nome, dados.equipa);
                                                equip.estado = 0;
                                                equip.pr_equipa = getpid();
                                                equip.quant_carros = 1;
                                                equip.pos_seg = 0;
                                                equip.carro_analisar = -1;

                                                pthread_cond_init(&equip.estado_box, NULL);
                                                pthread_mutex_init(&equip.mutex_estado_box, NULL);
                                                pthread_cond_init(&equip.carro_a_sair, NULL);
                                                pthread_mutex_init(&equip.mutex_carro_a_sair, NULL);

                                                shm->boxes[shm->quant_equipas] = equip;

                                                carro novo;
                                                novo.consumo = dados.consumo;
                                                novo.num_carro = dados.n_carro;
                                                novo.reabilidade = dados.reabilidade;
                                                novo.velocidade = dados.velocidade;
                                                strcpy(novo.nome_eq, dados.equipa);
                                                novo.combustivel = infos.cap_combus;
                                                novo.num_paragens_box = 0;
                                                novo.voltas = 0;
                                                novo.quer_box = 0;
                                                novo.dist_percorrida = 0;
                                                novo.id_box = shm->quant_equipas;
                                                novo.tem_avaria = 0;
                                                novo.dist_total = 0;
                                                novo.tempo_total = 0;
                                                shm->carros[(shm->quant_carros)] = novo;
                                                pipe(fd_unnamed_pipes[shm->quant_carros]);

                                                (shm->quant_carros)++;
                                                (shm->quant_equipas)++;

                                                snprintf(msg, MAXLINHA, "%02d:%02d:%02d NEW CAR LOADED => TEAM: %s CAR: %d SPEED: %f CONSUMPTION: %f RELIABILITY: %d.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec,dados.equipa, novo.num_carro, novo.velocidade, novo.consumo,novo.reabilidade);
                                                escrita_ficheiro(msg);
                                                fflush(stdout);
                                                fflush(f_log);
                                                sem_post(sem_shm);
                                                team_manager((shm->quant_equipas)-1);

                                                exit(0);
                                            }

                                    }
                                    else if ((shm->quant_equipas == infos.num_equipas) && (i == (shm->quant_equipas)-1)) {
                                        time(&rawtime);
                                        snprintf(msg, MAXLINHA, "%02d:%02d:%02d RACE IS FULL => TEAM %s REFUSED\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, dados.equipa);
                                        escrita_ficheiro(msg);
                                        break;
                                    }
                                }
                            }
                        }
                    }

                }
                else {
                     if ((strcmp(str, "START RACE!") != 0) && (strcmp(str, "ACABA") != 0)){
                        time(&rawtime);
                        snprintf(msg, MAXLINHA, "%02d:%02d:%02d Rejected, race already started!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
                        escrita_ficheiro(msg);
                     }
                }
            }
        }
        
        /*
        if (estado_corrida == 2) {
            for (int i = 0; i < shm->quant_carros; i++){
                close(fd_unnamed_pipes[i][1]);
                FD_ZERO(&read_set);
                FD_SET(fd_unnamed_pipes[i][0], &read_set);
                if (select(fd_unnamed_pipes[i][0]+1, &read_set, NULL, NULL, NULL) > 0 ) {
                    printf("UNNAMED PIPE %d\n", i);
                    nread = read(fd_unnamed_pipes[i][0], &est, MAXLINHA * sizeof(char));
                    est[nread - 1] = '\0';
                    printf("UNNAMED PIPE %d: %s\n", i, est);
                }
            }
        }
        */
        

        if ((shm->terminados == shm->quant_carros) && (shm->quant_carros != 0)) {
            time(&rawtime);
            snprintf(msg, MAXLINHA, "%02d:%02d:%02d CAR %d WINS THE RACE\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec, shm->carros[shm->vencedor].num_carro);
            escrita_ficheiro(msg);
            pthread_mutex_lock(&shm->mutex_estado_acabar);
            estado_corrida = 3;
            shm->estado_corrida = 3;
            pthread_cond_broadcast(&shm->estado_acabar);
            pthread_mutex_unlock(&shm->mutex_estado_acabar);
        }
    }
}

int main(int argc, char *argv[]){

    char msg[MAXLINHA];

    time(&rawtime);

    signal(SIGINT, sigint);
    signal(SIGTSTP, sigtstp);

    f_log = fopen("log.txt", "w");
    if (f_log == NULL){
        fprintf(stderr, "%02d:%02d:%02d Ficheiro 'log' não existe.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
    }

    time(&rawtime);

    //Cria o pipe de comunicação com o race manager
    if ((mkfifo(PIPE_NAME, O_CREAT|O_EXCL|0600)<0) && (errno != EEXIST)) {
        //fprintf(stderr, "%02d:%02d:%02d Erro ao criar o pipe!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        perror("ERRO");
        exit(1);
    }

    mqid = msgget(IPC_PRIVATE, IPC_CREAT|0777);
    if (mqid < 0){
        snprintf(msg, MAXLINHA, "%02d:%02d:%02d Erro na criação da message queue.\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        escrita_ficheiro(msg);
        exit(1);
    }

    // Cria semáforo
    sem_unlink("MUTEX");
    mutex = sem_open("MUTEX", O_CREAT | O_EXCL, 0700, 1);
    if (mutex == SEM_FAILED){
        fprintf(stderr, "%02d:%02d:%02d Erro ao criar o semaforo MUTEX!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        fprintf(f_log, "%02d:%02d:%02d Erro ao criar o semaforo MUTEX!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        exit(1);
    }

    // Cria semáforo
    sem_unlink("SEM_SHM");
    sem_shm = sem_open("SEM_SHM", O_CREAT | O_EXCL, 0700, 1);
    if (sem_shm == SEM_FAILED){
        fprintf(stderr, "%02d:%02d:%02d Erro ao criar o semaforo MUTEX!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        fprintf(f_log, "%02d:%02d:%02d Erro ao criar o semaforo MUTEX!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        exit(1);
    }

    // Cria semáforo
    sem_unlink("SEM_INICIO");
    sem_inicio = sem_open("SEM_INICIO", O_CREAT | O_EXCL, 0700, 0);
    if (sem_inicio == SEM_FAILED){
        fprintf(stderr, "%02d:%02d:%02d Erro ao criar o semaforo MUTEX!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        fprintf(f_log, "%02d:%02d:%02d Erro ao criar o semaforo MUTEX!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        exit(1);
    }

    time(&rawtime);
    snprintf(msg, MAXLINHA, "%02d:%02d:%02d SIMULATOR STARTING!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
    escrita_ficheiro(msg);

    time(&rawtime);
    // Cria memória partilhada
    if ((shmid = shmget(IPC_PRIVATE, sizeof(mem_par) + 105*sizeof(pid_t) + 4*sizeof(pthread_cond_t) + 4*sizeof(pthread_mutex_t) + 10 * sizeof(int) + sizeof(box)*infos.num_equipas + sizeof(carro) * infos.num_equipas * infos.max_carros_pequi * 2, IPC_CREAT | 0777)) < 0) {
        snprintf(msg, MAXLINHA, "%02d:%02d:%02d Erro no shmget com IPC_CREAT!\n", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        escrita_ficheiro(msg);
        exit(1);
    }

    time(&rawtime);
    // Atribui um espaço da memória partilhada
    shm = (mem_par *)shmat(shmid, NULL, 0);

    if (shm < (mem_par *)1) {
        snprintf(msg, MAXLINHA, "%02d:%02d:%02d Shmat error!", localtime(&rawtime)->tm_hour, localtime(&rawtime)->tm_min, localtime(&rawtime)->tm_sec);
        escrita_ficheiro(msg);
        exit(1);
    }

    pthread_mutexattr_init(&mutex_estado_preparacao);
    pthread_mutexattr_setpshared(&mutex_estado_preparacao, PTHREAD_PROCESS_SHARED);

    pthread_condattr_init(&estado_preparacao);
    pthread_condattr_setpshared(&estado_preparacao, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&shm->mutex_estado_preparacao, &mutex_estado_preparacao);

    pthread_cond_init(&shm->estado_preparacao, &estado_preparacao);

    pthread_mutexattr_init(&mutex_estado_comecar);
    pthread_mutexattr_setpshared(&mutex_estado_comecar, PTHREAD_PROCESS_SHARED);

    pthread_condattr_init(&estado_comecar);
    pthread_condattr_setpshared(&estado_comecar, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&shm->mutex_estado_comecar, &mutex_estado_comecar);

    pthread_cond_init(&shm->estado_comecar, &estado_comecar);

    pthread_mutexattr_init(&mutex_estado_acabar);
    pthread_mutexattr_setpshared(&mutex_estado_acabar, PTHREAD_PROCESS_SHARED);

    pthread_condattr_init(&estado_acabar);
    pthread_condattr_setpshared(&estado_acabar, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&shm->mutex_estado_acabar, &mutex_estado_acabar);

    pthread_cond_init(&shm->estado_acabar, &estado_acabar);

//------------------------------

    shm->estado_corrida = 0;
    shm->estado_corrida2 = 0;
    shm->quant_carros = 0;
    shm->quant_equipas = 0;
    shm->terminados = 0;
    shm->c= 0;
    shm->vencedor = -1;
    shm->quant_top = 0;
    shm->total_abastecimentos = 0;
    shm->total_avarias = 0;
    shm->flag_acabou = 0;
    
    readfile("configurations_file.txt", f_log); // Vai ler o ficheiro das configurações

    fflush(f_log); // Limpa todos os prints que já estão para trás

    time(&rawtime);
    // Cria processo gestor de corrida
    if (fork() == 0){
        pr_racemanager = getpid();
        printf("PID PARA SIGUSR1: %d\n", pr_racemanager);
        race_manager();
        exit(0);
    }

    time(&rawtime);
    // Cria processo gestor de avarias
    if (fork() == 0){
        pr_avarias = getpid();
        gestor_avarias();
        exit(0);
    }

    wait(NULL);
    wait(NULL);

    finish();   // Vai terminar os recursos utilizados

    return 0;
}
