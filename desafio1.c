#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

// Estructura para almacenar información de los procesos
typedef struct {
    pid_t pid;      // PID del proceso
    int activo;     // 1 si está activo, 0 si fue eliminado
} Proceso;

// Variables globales
volatile sig_atomic_t token_recibido = 0;    // Indica si se recibió el token
volatile sig_atomic_t current_token = 0;     // Valor actual del token
volatile sig_atomic_t eliminado = 0;         // Indica si el proceso fue eliminado
volatile sig_atomic_t eleccion_activa = 0;   // Indica si hay una elección en curso
volatile sig_atomic_t nuevo_lider = 0;       // Indica si este proceso es el nuevo líder
volatile sig_atomic_t procesos_activos = 0;  // Contador de procesos activos
volatile sig_atomic_t ultimo_token = 0;      // Último valor del token enviado/recibido

Proceso *procesos;  // Array de procesos
int num_procesos;   // Número total de procesos
int M;              // Valor máximo para restar al token
int token_inicial;  // Valor inicial del token
pid_t mi_pid;       // PID del proceso actual
int mi_indice;      // Índice del proceso en el array
int es_padre = 1;   // Indica si es el proceso padre

// Señales usadas
#define SIG_TOKEN SIGUSR1      // Para enviar el token
#define SIG_ELIMINADO SIGUSR2  // Para notificar eliminación
#define SIG_ELECCION SIGRTMIN  // Para iniciar elección de líder
#define SIG_LIDER SIGRTMIN+1   // Para anunciar nuevo líder

// Prototipos de funciones
void configurar_senales();
void manejador_token(int sig, siginfo_t *si, void *context);
void manejador_eliminado(int sig, siginfo_t *si, void *context);
void manejador_eleccion(int sig, siginfo_t *si, void *context);
void manejador_lider(int sig, siginfo_t *si, void *context);
void enviar_token(pid_t destino, int token);
void enviar_eliminado();  // Corregido: sin parámetros
void iniciar_eleccion();
void anunciar_lider();
pid_t obtener_siguiente_proceso();
void procesar_token();
void imprimir_resultado(int token_original, int token_resultante);
void limpiar_recursos();
void esperar_token();

int main(int argc, char *argv[]) {
    int opt;
    num_procesos = 0;
    M = 0;
    token_inicial = 0;
    
    // Procesar argumentos de línea de comandos
    while ((opt = getopt(argc, argv, "p:t:M:")) != -1) {
        switch (opt) {
            case 'p':
                num_procesos = atoi(optarg);
                if (num_procesos <= 0) {
                    fprintf(stderr, "El número de procesos debe ser positivo\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 't':
                token_inicial = atoi(optarg);
                if (token_inicial <= 0) {
                    fprintf(stderr, "El token inicial debe ser positivo\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'M':
                M = atoi(optarg);
                if (M <= 0) {
                    fprintf(stderr, "M debe ser positivo\n");
                    exit(EXIT_FAILURE);
                }
                break;
            default:
                fprintf(stderr, "Uso: %s -p <num_procesos> -t <token_inicial> -M <max_resta>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    
    // Validar que se hayan proporcionado todos los argumentos requeridos
    if (num_procesos == 0 || token_inicial == 0 || M == 0) {
        fprintf(stderr, "Uso: %s -p <num_procesos> -t <token_inicial> -M <max_resta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Inicializar generador de números aleatorios
    srand(time(NULL) ^ getpid());
    
    // Inicializar array de procesos
    procesos = (Proceso *)malloc(num_procesos * sizeof(Proceso));
    if (procesos == NULL) {
        perror("Error en malloc");
        exit(EXIT_FAILURE);
    }
    
    // Configurar señales para el proceso padre
    configurar_senales();
    
    // Crear procesos hijos
    for (int i = 0; i < num_procesos; i++) {
        procesos[i].activo = 1;
        
        if (i > 0) { // Si no es el primer proceso, crear un hijo
            pid_t pid = fork();
            
            if (pid == -1) {
                perror("fork");
                limpiar_recursos();
                exit(EXIT_FAILURE);
            } else if (pid == 0) { // Proceso hijo
                es_padre = 0;
                mi_indice = i;
                mi_pid = getpid();
                break;
            } else { // Proceso padre
                procesos[i].pid = pid;
            }
        } else { // El primer proceso es el padre
            procesos[0].pid = getpid();
            mi_pid = getpid();
            mi_indice = 0;
        }
    }
    
    // Esperar a que todos los procesos estén creados
    sleep(1);
    
    // Completar la información de PIDs para todos los procesos
    for (int i = 0; i < num_procesos; i++) {
        if (i == mi_indice) {
            procesos[i].pid = mi_pid;
        }
        
        // Comunicar los PIDs a todos los procesos (en un escenario real se usaría IPC, 
        // pero para simplificar este ejemplo asumimos que todos los procesos conocen los PIDs)
    }
    
    procesos_activos = num_procesos;
    
    // Si es el proceso padre (índice 0), iniciar el desafío
    if (mi_indice == 0) {
        // Iniciar el desafío enviando el token al primer proceso
        sleep(2); // Dar tiempo a que todos los procesos estén listos
        procesar_token();
    } else {
        // Los procesos hijos esperan a recibir el token
        esperar_token();
    }
    
    // Si es el proceso padre, esperar a que terminen todos los hijos
    if (es_padre) {
        for (int i = 1; i < num_procesos; i++) {
            wait(NULL);
        }
        
        printf("Juego terminado\n");
        limpiar_recursos();
    }
    
    return 0;
}

// Configurar los manejadores de señales
void configurar_senales() {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    
    // Configurar manejador para señal de token (SIGUSR1)
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = manejador_token;
    if (sigaction(SIG_TOKEN, &sa, NULL) == -1) {
        perror("sigaction (SIG_TOKEN)");
        exit(EXIT_FAILURE);
    }
    
    // Configurar manejador para señal de eliminación (SIGUSR2)
    sa.sa_sigaction = manejador_eliminado;
    if (sigaction(SIG_ELIMINADO, &sa, NULL) == -1) {
        perror("sigaction (SIG_ELIMINADO)");
        exit(EXIT_FAILURE);
    }
    
    // Configurar manejador para señal de elección (SIGRTMIN)
    sa.sa_sigaction = manejador_eleccion;
    if (sigaction(SIG_ELECCION, &sa, NULL) == -1) {
        perror("sigaction (SIG_ELECCION)");
        exit(EXIT_FAILURE);
    }
    
    // Configurar manejador para señal de líder (SIGRTMIN+1)
    sa.sa_sigaction = manejador_lider;
    if (sigaction(SIG_LIDER, &sa, NULL) == -1) {
        perror("sigaction (SIG_LIDER)");
        exit(EXIT_FAILURE);
    }
}

// Manejador para la señal de token
void manejador_token(int sig, siginfo_t *si, void *context) {
    (void)sig;      // Evitar warning de parámetro no utilizado
    (void)context;  // Evitar warning de parámetro no utilizado
    
    if (eliminado) return; // Si el proceso ya fue eliminado, ignorar la señal
    
    // Extraer el token enviado
    current_token = si->si_value.sival_int;
    token_recibido = 1;
}

// Manejador para la señal de proceso eliminado
void manejador_eliminado(int sig, siginfo_t *si, void *context) {
    (void)sig;      // Evitar warning de parámetro no utilizado
    (void)context;  // Evitar warning de parámetro no utilizado
    
    // Marcar el proceso como eliminado
    for (int i = 0; i < num_procesos; i++) {
        if (procesos[i].pid == si->si_pid) {
            procesos[i].activo = 0;
            procesos_activos--;
            break;
        }
    }
    
    // Si somos el único proceso activo, somos el ganador
    if (procesos_activos == 1 && !eliminado) {
        printf("Proceso %d es el ganador\n", mi_indice);
        exit(EXIT_SUCCESS);
    }
    
    // Si no hay un proceso activo que sea el líder, iniciar elección
    if (!eliminado && !eleccion_activa) {
        iniciar_eleccion();
    }
}

// Manejador para la señal de elección de líder
void manejador_eleccion(int sig, siginfo_t *si, void *context) {
    (void)sig;      // Evitar warning de parámetro no utilizado
    (void)context;  // Evitar warning de parámetro no utilizado
    
    if (eliminado) return; // Si el proceso ya fue eliminado, ignorar la señal
    
    // Si recibimos un PID mayor que el nuestro, propagar la elección
    int pid_enviado = si->si_value.sival_int;
    
    // Si nuestro PID es mayor que el recibido, iniciar nuestra propia elección
    if (mi_pid > pid_enviado) {
        iniciar_eleccion();
    } else if (!eleccion_activa) { // Si no estamos en una elección, propagar
        eleccion_activa = 1;
        
        // Propagar la elección al siguiente proceso activo
        pid_t siguiente = obtener_siguiente_proceso();
        if (siguiente != -1) {
            union sigval value;
            value.sival_int = pid_enviado;
            sigqueue(siguiente, SIG_ELECCION, value);
        }
    }
}

// Manejador para la señal de nuevo líder
void manejador_lider(int sig, siginfo_t *si, void *context) {
    (void)sig;      // Evitar warning de parámetro no utilizado
    (void)context;  // Evitar warning de parámetro no utilizado
    
    if (eliminado) return; // Si el proceso ya fue eliminado, ignorar la señal
    
    pid_t lider_pid = si->si_value.sival_int;
    
    // Si somos el líder, reinicializar el token y reanudar el desafío
    if (lider_pid == mi_pid) {
        nuevo_lider = 1;
        eleccion_activa = 0;
        
        // Reinicializar el token y reanudar el desafío
        current_token = token_inicial;
        procesar_token();
    } else {
        // Propagar la señal de líder al siguiente proceso activo
        eleccion_activa = 0;
        
        pid_t siguiente = obtener_siguiente_proceso();
        if (siguiente != -1 && siguiente != lider_pid) {
            union sigval value;
            value.sival_int = lider_pid;
            sigqueue(siguiente, SIG_LIDER, value);
        }
    }
}

// Enviar token al proceso destino
void enviar_token(pid_t destino, int token) {
    union sigval value;
    value.sival_int = token;
    
    if (sigqueue(destino, SIG_TOKEN, value) == -1) {
        perror("sigqueue (token)");
    }
}

// Notificar a todos los procesos que este proceso ha sido eliminado
void enviar_eliminado() {
    for (int i = 0; i < num_procesos; i++) {
        if (procesos[i].activo && procesos[i].pid != mi_pid) {
            union sigval value;
            value.sival_int = mi_indice;
            sigqueue(procesos[i].pid, SIG_ELIMINADO, value);
        }
    }
}

// Iniciar proceso de elección de líder
void iniciar_eleccion() {
    if (eleccion_activa || eliminado) return;
    
    eleccion_activa = 1;
    
    // Iniciar elección enviando nuestro PID
    pid_t siguiente = obtener_siguiente_proceso();
    if (siguiente != -1) {
        union sigval value;
        value.sival_int = mi_pid;
        sigqueue(siguiente, SIG_ELECCION, value);
    } else {
        // Si no hay más procesos, somos el líder
        anunciar_lider();
    }
}

// Anunciar que este proceso es el nuevo líder
void anunciar_lider() {
    nuevo_lider = 1;
    eleccion_activa = 0;
    
    // Notificar a todos los procesos activos que somos el líder
    for (int i = 0; i < num_procesos; i++) {
        if (procesos[i].activo && procesos[i].pid != mi_pid) {
            union sigval value;
            value.sival_int = mi_pid;
            sigqueue(procesos[i].pid, SIG_LIDER, value);
        }
    }
    
    // Reinicializar el token y reanudar el desafío
    current_token = token_inicial;
    procesar_token();
}

// Obtener el siguiente proceso activo en el anillo
pid_t obtener_siguiente_proceso() {
    int siguiente_indice = (mi_indice + 1) % num_procesos;
    int contador = 0;
    
    while (contador < num_procesos) {
        if (procesos[siguiente_indice].activo && siguiente_indice != mi_indice) {
            return procesos[siguiente_indice].pid;
        }
        siguiente_indice = (siguiente_indice + 1) % num_procesos;
        contador++;
    }
    
    return -1; // No hay más procesos activos
}

// Procesar el token recibido
void procesar_token() {
    if (eliminado) return;
    
    int token_original = current_token;
    int valor_resta = rand() % M;
    int token_resultante = token_original - valor_resta;
    
    imprimir_resultado(token_original, token_resultante);
    
    if (token_resultante < 0) {
        // El proceso es eliminado
        eliminado = 1;
        printf("Proceso %d; Token recibido: %d; Token resultante: %d (Proceso %d es eliminado)\n", 
               mi_indice, token_original, token_resultante, mi_indice);
        
        // Notificar a todos los procesos que ha sido eliminado
        enviar_eliminado();
        
        if (procesos_activos == 1) {
            // Si somos el último proceso, salir
            exit(EXIT_SUCCESS);
        }
        
        // El proceso eliminado espera a que termine el juego
        while (1) {
            sleep(10);
        }
    } else {
        // Enviar el token al siguiente proceso activo
        current_token = token_resultante;
        ultimo_token = token_resultante;
        
        pid_t siguiente = obtener_siguiente_proceso();
        if (siguiente != -1) {
            enviar_token(siguiente, token_resultante);
        } else if (procesos_activos == 1) {
            // Si somos el único proceso activo, somos el ganador
            printf("Proceso %d es el ganador\n", mi_indice);
            exit(EXIT_SUCCESS);
        }
    }
}

// Imprimir el resultado del procesamiento del token
void imprimir_resultado(int token_original, int token_resultante) {
    if (token_resultante >= 0) {
        printf("Proceso %d; Token recibido: %d; Token resultante: %d\n", 
               mi_indice, token_original, token_resultante);
    }
}

// Liberar recursos
void limpiar_recursos() {
    if (procesos != NULL) {
        free(procesos);
        procesos = NULL;
    }
}

// Esperar a recibir el token
void esperar_token() {
    sigset_t mask, oldmask;
    
    // Bloquear todas las señales excepto las que manejamos
    sigfillset(&mask);
    sigdelset(&mask, SIG_TOKEN);
    sigdelset(&mask, SIG_ELIMINADO);
    sigdelset(&mask, SIG_ELECCION);
    sigdelset(&mask, SIG_LIDER);
    sigdelset(&mask, SIGTERM);
    sigdelset(&mask, SIGINT);
    
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    
    while (!eliminado) {
        // Esperar a que llegue alguna señal
        sigsuspend(&oldmask);
        
        // Si se recibió el token, procesarlo
        if (token_recibido) {
            token_recibido = 0;
            procesar_token();
        }
        
        // Si somos el nuevo líder, procesar el token
        if (nuevo_lider) {
            nuevo_lider = 0;
            current_token = token_inicial;
            procesar_token();
        }
    }
    
    // Si el proceso es eliminado, esperar indefinidamente
    while (1) {
        sigsuspend(&oldmask);
    }
}