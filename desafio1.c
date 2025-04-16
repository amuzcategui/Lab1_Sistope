#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>

#define MAX_PROCESOS 100

// Estructura para almacenar información sobre los procesos
typedef struct {
    pid_t pid;        // PID del proceso
    int index;        // Índice del proceso en el anillo (0 a n-1)
    bool activo;      // Indica si el proceso está activo
} Proceso;

// Variables globales para manejo de señales
volatile sig_atomic_t token_recibido = 0;
volatile sig_atomic_t token_valor = 0;
volatile sig_atomic_t proceso_eliminado = 0;
volatile sig_atomic_t eliminado_idx = -1;
volatile sig_atomic_t lider_idx = -1;
volatile sig_atomic_t received_pid_count = 0;

// Información global sobre los procesos
Proceso procesos[MAX_PROCESOS];
int num_procesos = 0;
int mi_indice = -1;
int token_inicial = 0;
int max_random = 0;
int procesos_activos = 0;

// Señales utilizadas
#define SIGNAL_TOKEN SIGUSR1
#define SIGNAL_ELIMINACION SIGUSR2
#define SIGNAL_LIDER SIGRTMIN
#define SIGNAL_PID SIGRTMIN+1

// Manejador de la señal para recibir el token
void manejador_token(int sig, siginfo_t *si, void *context) {
    token_valor = si->si_value.sival_int;
    token_recibido = 1;
}

// Manejador de la señal para notificación de proceso eliminado
// Manejador de la señal para notificación de proceso eliminado
void manejador_eliminacion(int sig, siginfo_t *si, void *context) {
    eliminado_idx = si->si_value.sival_int;
    procesos[eliminado_idx].activo = false;
    procesos_activos--;
    proceso_eliminado = 1;
    
    
}

// Manejador de la señal para notificación de nuevo líder
void manejador_lider(int sig, siginfo_t *si, void *context) {
    lider_idx = si->si_value.sival_int;
}

// Manejador para recibir los PIDs de los procesos
void manejador_pid(int sig, siginfo_t *si, void *context) {
    int index = si->si_value.sival_int >> 16;
    pid_t pid = si->si_value.sival_int & 0xFFFF;
    
    if (index >= 0 && index < num_procesos) {
        procesos[index].pid = pid;
        received_pid_count++;
    }
}

// Configura los manejadores de señales
void configurar_manejadores() {
    struct sigaction sa;
    
    // Configurar manejador para el token (SIGUSR1)
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = manejador_token;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGNAL_TOKEN, &sa, NULL) == -1) {
        perror("sigaction token");
        exit(EXIT_FAILURE);
    }
    
    // Configurar manejador para eliminación (SIGUSR2)
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = manejador_eliminacion;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGNAL_ELIMINACION, &sa, NULL) == -1) {
        perror("sigaction eliminacion");
        exit(EXIT_FAILURE);
    }
    
    // Configurar manejador para nuevo líder (SIGRTMIN)
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = manejador_lider;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGNAL_LIDER, &sa, NULL) == -1) {
        perror("sigaction lider");
        exit(EXIT_FAILURE);
    }
    
    // Configurar manejador para recibir PIDs (SIGRTMIN+1)
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = manejador_pid;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGNAL_PID, &sa, NULL) == -1) {
        perror("sigaction pid");
        exit(EXIT_FAILURE);
    }
}

// Envía el token al proceso destino
void enviar_token(int destino_idx, int valor) {
    union sigval value;
    value.sival_int = valor;
    
    if (sigqueue(procesos[destino_idx].pid, SIGNAL_TOKEN, value) == -1) {
        perror("sigqueue token");
    }
}

// Notifica a todos los procesos que un proceso ha sido eliminado
void notificar_eliminacion(int eliminado_idx) {
    union sigval value;
    value.sival_int = eliminado_idx;
    
    for (int i = 0; i < num_procesos; i++) {
        if (procesos[i].activo && i != mi_indice) {
            if (sigqueue(procesos[i].pid, SIGNAL_ELIMINACION, value) == -1) {
                perror("sigqueue eliminacion");
            }
        }
    }
}

// Notifica a todos los procesos quién es el nuevo líder y reinicia el token
void anunciar_lider(int lider_idx) {
    union sigval value;
    value.sival_int = lider_idx;
    
    for (int i = 0; i < num_procesos; i++) {
        if (procesos[i].activo && i != mi_indice) {
            if (sigqueue(procesos[i].pid, SIGNAL_LIDER, value) == -1) {
                perror("sigqueue lider");
            }
        }
    }
}

// Encuentra el siguiente proceso activo en el anillo
int siguiente_proceso_activo() {
    int siguiente = (mi_indice + 1) % num_procesos;
    
    while (!procesos[siguiente].activo && siguiente != mi_indice) {
        siguiente = (siguiente + 1) % num_procesos;
    }
    
    return siguiente;
}

// Encuentra el proceso activo con el mayor índice (para elección de líder)
int proceso_con_mayor_indice() {
    int max_idx = -1;
    
    for (int i = 0; i < num_procesos; i++) {
        if (procesos[i].activo && (max_idx == -1 || i > max_idx)) {
            max_idx = i;
        }
    }
    
    return max_idx;
}

// Genera un número aleatorio para decrementar el token
int generar_aleatorio() {
    return rand() % max_random;
}

int main(int argc, char *argv[]) {
    // Variables para manejar opciones de línea de comandos
    int opt;
    int num_hijos = 0;
    
    // Procesar opciones de línea de comandos
    while ((opt = getopt(argc, argv, "p:t:M:")) != -1) {
        switch (opt) {
            case 'p':
                num_hijos = atoi(optarg);
                if (num_hijos <= 0 || num_hijos > MAX_PROCESOS) {
                    fprintf(stderr, "Número de procesos debe ser entre 1 y %d\n", MAX_PROCESOS);
                    exit(EXIT_FAILURE);
                }
                break;
            case 't':
                token_inicial = atoi(optarg);
                if (token_inicial <= 0) {
                    fprintf(stderr, "El token inicial debe ser un valor positivo\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'M':
                max_random = atoi(optarg);
                if (max_random <= 0) {
                    fprintf(stderr, "El valor máximo aleatorio debe ser positivo\n");
                    exit(EXIT_FAILURE);
                }
                break;
            default:
                fprintf(stderr, "Uso: %s -p <num_procesos> -t <token_inicial> -M <max_random>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    
    // Verificar que se hayan proporcionado todas las opciones requeridas
    if (num_hijos == 0 || token_inicial == 0 || max_random == 0) {
        fprintf(stderr, "Uso: %s -p <num_procesos> -t <token_inicial> -M <max_random>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Inicializar la semilla para números aleatorios
    srand(time(NULL) ^ getpid());
    
    // Inicializar datos de los procesos
    num_procesos = num_hijos;
    procesos_activos = num_procesos;
    
    for (int i = 0; i < num_procesos; i++) {
        procesos[i].index = i;
        procesos[i].activo = true;
        procesos[i].pid = -1; // Inicialmente desconocido
    }
    
    // Crear máscara para bloquear señales temporalmente
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGNAL_TOKEN);
    sigaddset(&mask, SIGNAL_ELIMINACION);
    sigaddset(&mask, SIGNAL_LIDER);
    sigaddset(&mask, SIGNAL_PID);
    
    // Bloquear señales durante la creación de procesos
    if (sigprocmask(SIG_BLOCK, &mask, &oldmask) < 0) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }
    
    // Crear procesos hijos
    pid_t pid;
    for (int i = 0; i < num_procesos; i++) {
        if (i > 0) {  // Crear procesos hijos a partir del segundo
            pid = fork();
            
            if (pid == -1) {
                perror("fork");
                exit(EXIT_FAILURE);
            }
            
            if (pid == 0) {  // Proceso hijo
                mi_indice = i;
                procesos[i].pid = getpid();
                break;  // Sale del bucle para no crear más hijos
            } else {  // Proceso padre
                procesos[i].pid = pid;
            }
        } else {  // Proceso inicial (padre)
            mi_indice = 0;
            procesos[0].pid = getpid();
        }
    }
    
    // Configurar manejadores de señales
    configurar_manejadores();
    
    // Compartir PIDs entre procesos
    if (mi_indice == 0) {  // El proceso principal envía los PIDs a todos los hijos
        // Esperar un poco para asegurar que todos los procesos estén listos
        usleep(100000);
        
        // Enviar información de PIDs a todos los procesos
        for (int i = 1; i < num_procesos; i++) {
            for (int j = 0; j < num_procesos; j++) {
                union sigval value;
                // Codificar el índice y PID en un solo entero
                // Los 16 bits superiores llevan el índice, los 16 inferiores el PID
                value.sival_int = (j << 16) | (procesos[j].pid & 0xFFFF);
                
                if (sigqueue(procesos[i].pid, SIGNAL_PID, value) == -1) {
                    perror("sigqueue PID");
                }
                usleep(10000);  // Pequeña pausa para evitar congestión
            }
        }
    } else {  // Procesos hijos esperan recibir todos los PIDs
        while (received_pid_count < num_procesos) {
            sigsuspend(&oldmask);
        }
    }
    
    // Desbloquear las señales
    if (sigprocmask(SIG_SETMASK, &oldmask, NULL) < 0) {
        perror("sigprocmask unblock");
        exit(EXIT_FAILURE);
    }
    
    // Sincronizar todos los procesos antes de comenzar
    sleep(1);
    
    // El proceso 0 inicia el juego enviando el token
    if (mi_indice == 0) {
        int siguiente = siguiente_proceso_activo();
        printf("Proceso %d; Token recibido: %d; Token resultante: %d\n", 
               mi_indice, token_inicial, token_inicial);
        enviar_token(siguiente, token_inicial);
    }
    
    // Bucle principal de cada proceso
    while (procesos_activos > 1 || (procesos_activos == 1 && !procesos[mi_indice].activo)) {
        // Esperar a recibir una señal
        pause();
        
        // Procesar el token si fue recibido
        if (token_recibido) {
            token_recibido = 0;
            
            // Decrementar el token
            int decremento = generar_aleatorio();
            int nuevo_token = token_valor - decremento;
            
            printf("Proceso %d; Token recibido: %d; Token resultante: %d\n", 
                   mi_indice, token_valor, nuevo_token);
            
            // Si el token es negativo, este proceso se elimina
            // Si el token es negativo, este proceso se elimina
        if (nuevo_token < 0) {
            printf("Proceso %d es eliminado\n", mi_indice);
            procesos[mi_indice].activo = false;
            procesos_activos--;
            
            // Notify all other processes about the elimination
            notificar_eliminacion(mi_indice);
            
            // If more than one active process remains, elect a new leader
            if (procesos_activos > 1) {
                int nuevo_lider = proceso_con_mayor_indice();
                
                // Announce the new leader (whether this was the leader or not)
                anunciar_lider(nuevo_lider);
                
                // If this process was not the leader and the new leader should start the token
                if (mi_indice == 0 || nuevo_lider == mi_indice) {
                    // If we're the new leader, send the token
                    if (nuevo_lider == mi_indice) {
                        int siguiente = siguiente_proceso_activo();
                        if (siguiente != mi_indice) {
                            enviar_token(siguiente, token_inicial);
                        }
                    }
                }
            }
            continue; // Don't send token, wait for the game to end
        }
            
            // Enviar el token al siguiente proceso
            int siguiente = siguiente_proceso_activo();
            if (siguiente != mi_indice) {
                enviar_token(siguiente, nuevo_token);
            }
        }
        
        // Si un proceso fue eliminado
        if (proceso_eliminado) {
            proceso_eliminado = 0;
            
            // Si este proceso es ahora el líder (mayor índice), reiniciar el token
            if (lider_idx == mi_indice) {
                int siguiente = siguiente_proceso_activo();
                if (siguiente != mi_indice) {
                    enviar_token(siguiente, token_inicial);
                }
            }
        }
        
        // Si se recibió un anuncio de nuevo líder
        if (lider_idx != -1) {
            // Si este proceso es el nuevo líder, reiniciar el token
            if (lider_idx == mi_indice) {
                int siguiente = siguiente_proceso_activo();
                if (siguiente != mi_indice) {
                    enviar_token(siguiente, token_inicial);
                }
            }
            lider_idx = -1;
        }
    }
    
    // Si este proceso es el único activo, es el ganador
    if (procesos[mi_indice].activo && procesos_activos == 1) {
        printf("Proceso %d es el ganador\n", mi_indice);
        exit(0);
    }
    
    // El proceso principal espera a que todos los hijos terminen
    if (mi_indice == 0) {
        for (int i = 1; i < num_procesos; i++) {
            wait(NULL);
        }
    }
    
    return 0;
}