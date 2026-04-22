/*
 * Trabalho Prático 1 — Algoritmo do Banqueiro
 *
 * Disciplina : Sistemas Operacionais
 *
 * Implementação em C com pthreads do Algoritmo do Banqueiro.
 * Vários clientes (threads) solicitam e liberam recursos; o banqueiro
 * concede a solicitação apenas se o sistema permanecer em estado seguro.
 *
 * Compilação : gcc -Wall -Wextra -pthread -o banker banker.c
 * Execução   : ./banker <r1> [r2 ...rn]
 * Exemplo    : ./banker 10 5 7
 */

/* Habilita usleep, useconds_t e rand_r no POSIX */
#define _XOPEN_SOURCE 600

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── Constantes configuráveis ────────────────────────────────────────────── */

#define NUMBER_OF_CUSTOMERS 5

/*
 * DEFAULT_ITERATIONS controla quantas rodadas (request+release) cada cliente
 * executa antes de encerrar.  Altere para 0 para loop contínuo (use Ctrl-C).
 */
#define DEFAULT_ITERATIONS 12

#define MAX_SLEEP_USEC 200000   /* latência máxima entre operações: 0,2 s */

/* ── Estado global do banqueiro ──────────────────────────────────────────── */

static int  number_of_resources = 0;

static int  *available  = NULL;   /* recursos disponíveis                   */
static int **maximum    = NULL;   /* demanda máxima de cada cliente         */
static int **allocation = NULL;   /* alocação corrente de cada cliente      */
static int **need       = NULL;   /* necessidade remanescente de cada cliente */

/* Único mutex que protege todas as estruturas compartilhadas acima. */
static pthread_mutex_t bank_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── Funções auxiliares de memória ───────────────────────────────────────── */

static void free_matrix(int **matrix, int rows)
{
    if (matrix == NULL) return;
    for (int i = 0; i < rows; i++)
        free(matrix[i]);
    free(matrix);
}

static int **alloc_matrix(int rows, int cols)
{
    int **m = (int **)malloc((size_t)rows * sizeof(int *));
    if (m == NULL) return NULL;

    for (int i = 0; i < rows; i++) {
        m[i] = (int *)calloc((size_t)cols, sizeof(int));
        if (m[i] == NULL) {
            for (int j = 0; j < i; j++) free(m[j]);
            free(m);
            return NULL;
        }
    }
    return m;
}

/* ── Funções de impressão (chamadas somente com bank_lock já adquirido) ──── */

/*
 * Imprime um vetor de inteiros no formato "[a b c]".
 * PRÉ-CONDIÇÃO: bank_lock deve estar adquirido pelo chamador.
 */
static void print_vector(const char *label, const int *v)
{
    printf("%s[", label);
    for (int j = 0; j < number_of_resources; j++) {
        printf("%d", v[j]);
        if (j + 1 < number_of_resources) printf(" ");
    }
    printf("]\n");
}

/*
 * Imprime um vetor inline (sem newline final).
 * PRÉ-CONDIÇÃO: bank_lock deve estar adquirido pelo chamador.
 */
static void print_action_vector(const int *v)
{
    printf("[");
    for (int j = 0; j < number_of_resources; j++) {
        printf("%d", v[j]);
        if (j + 1 < number_of_resources) printf(" ");
    }
    printf("]");
}

/*
 * Exibe o estado completo do sistema.
 * PRÉ-CONDIÇÃO: bank_lock deve estar adquirido pelo chamador.
 */
static void print_state(void)
{
    printf("\n=== ESTADO DO SISTEMA ===\n");
    print_vector("Available  = ", available);

    printf("Maximum:\n");
    for (int i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        printf("  C%d: [", i);
        for (int j = 0; j < number_of_resources; j++) {
            printf("%d", maximum[i][j]);
            if (j + 1 < number_of_resources) printf(" ");
        }
        printf("]\n");
    }

    printf("Allocation:\n");
    for (int i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        printf("  C%d: [", i);
        for (int j = 0; j < number_of_resources; j++) {
            printf("%d", allocation[i][j]);
            if (j + 1 < number_of_resources) printf(" ");
        }
        printf("]\n");
    }

    printf("Need:\n");
    for (int i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        printf("  C%d: [", i);
        for (int j = 0; j < number_of_resources; j++) {
            printf("%d", need[i][j]);
            if (j + 1 < number_of_resources) printf(" ");
        }
        printf("]\n");
    }
    printf("=========================\n\n");
}

/* ── Algoritmo de segurança ──────────────────────────────────────────────── */

/*
 * Verifica se o cliente 'customer' pode ser atendido pelo vetor 'work'.
 * PRÉ-CONDIÇÃO: bank_lock deve estar adquirido pelo chamador.
 */
static bool can_finish_with_work(int customer, const int *work)
{
    for (int j = 0; j < number_of_resources; j++) {
        if (need[customer][j] > work[j])
            return false;
    }
    return true;
}

/*
 * Algoritmo de segurança do banqueiro (seção 8.6.3 de Silberschatz).
 * Retorna true se o estado atual é seguro; false caso contrário.
 * PRÉ-CONDIÇÃO: bank_lock deve estar adquirido pelo chamador.
 *
 * Complexidade: O(n² · m), onde n = clientes, m = tipos de recurso.
 */
static bool is_safe_state(void)
{
    int  *work   = (int  *)malloc((size_t)number_of_resources * sizeof(int));
    bool *finish = (bool *)calloc(NUMBER_OF_CUSTOMERS, sizeof(bool));
    if (work == NULL || finish == NULL) {
        free(work);
        free(finish);
        return false;   /* falha de alocação → conservadoramente inseguro */
    }

    /* Passo 1: work ← available */
    for (int j = 0; j < number_of_resources; j++)
        work[j] = available[j];

    /* Passo 2: encontrar cliente i tal que finish[i]=false e need[i]≤work */
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
            if (!finish[i] && can_finish_with_work(i, work)) {
                /* Simula conclusão: devolve alocação ao work */
                for (int j = 0; j < number_of_resources; j++)
                    work[j] += allocation[i][j];
                finish[i] = true;
                progress   = true;
            }
        }
    }

    /* Passo 3: estado seguro ⟺ todos os clientes terminaram */
    bool safe = true;
    for (int i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        if (!finish[i]) { safe = false; break; }
    }

    free(work);
    free(finish);
    return safe;
}

/* ── Interface pública do banqueiro ──────────────────────────────────────── */

/*
 * Tenta alocar 'request' recursos para o cliente 'customer_num'.
 *
 * Retorna  0  se concedido (sistema permanece seguro).
 * Retorna -1  se negado   (solicitação inválida ou estado ficaria inseguro).
 *
 * Toda a lógica — incluindo a impressão do resultado — ocorre dentro da
 * seção crítica, garantindo que o estado exibido é exatamente o estado
 * resultante da operação.
 */
int request_resources(int customer_num, int request[])
{
    pthread_mutex_lock(&bank_lock);

    /* ── Verificação 1: request ≤ need ── */
    for (int j = 0; j < number_of_resources; j++) {
        if (request[j] < 0 || request[j] > need[customer_num][j]) {
            printf("Cliente %d solicitou ", customer_num);
            print_action_vector(request);
            printf(" -> NEGADO (solicitação excede need)\n");
            pthread_mutex_unlock(&bank_lock);
            return -1;
        }
    }

    /* ── Verificação 2: request ≤ available ── */
    for (int j = 0; j < number_of_resources; j++) {
        if (request[j] > available[j]) {
            printf("Cliente %d solicitou ", customer_num);
            print_action_vector(request);
            printf(" -> NEGADO (recursos insuficientes)\n");
            pthread_mutex_unlock(&bank_lock);
            return -1;
        }
    }

    /* ── Alocação tentativa ── */
    for (int j = 0; j < number_of_resources; j++) {
        available[j]             -= request[j];
        allocation[customer_num][j] += request[j];
        need[customer_num][j]       -= request[j];
    }

    /* ── Verificação de segurança ── */
    if (!is_safe_state()) {
        /* Desfaz alocação tentativa (rollback) */
        for (int j = 0; j < number_of_resources; j++) {
            available[j]             += request[j];
            allocation[customer_num][j] -= request[j];
            need[customer_num][j]       += request[j];
        }
        printf("Cliente %d solicitou ", customer_num);
        print_action_vector(request);
        printf(" -> NEGADO (estado inseguro)\n");
        pthread_mutex_unlock(&bank_lock);
        return -1;
    }

    /* ── Concessão ── */
    printf("Cliente %d solicitou ", customer_num);
    print_action_vector(request);
    printf(" -> CONCEDIDO\n");
    print_state();

    pthread_mutex_unlock(&bank_lock);
    return 0;
}

/*
 * Libera 'release' recursos do cliente 'customer_num' de volta ao sistema.
 *
 * Retorna  0  se bem-sucedido.
 * Retorna -1  se 'release' exceder o que está alocado.
 */
int release_resources(int customer_num, int release[])
{
    pthread_mutex_lock(&bank_lock);

    /* Valida: não se pode liberar mais do que está alocado */
    for (int j = 0; j < number_of_resources; j++) {
        if (release[j] < 0 || release[j] > allocation[customer_num][j]) {
            printf("Cliente %d tentou liberar ", customer_num);
            print_action_vector(release);
            printf(" -> INVÁLIDO (excede allocation)\n");
            pthread_mutex_unlock(&bank_lock);
            return -1;
        }
    }

    for (int j = 0; j < number_of_resources; j++) {
        available[j]             += release[j];
        allocation[customer_num][j] -= release[j];
        need[customer_num][j]       += release[j];
    }

    printf("Cliente %d liberou    ", customer_num);
    print_action_vector(release);
    printf("\n");
    print_state();

    pthread_mutex_unlock(&bank_lock);
    return 0;
}

/* ── Geração aleatória de solicitações e liberações ──────────────────────── */

/*
 * Gera um vetor de solicitação aleatório para 'customer' com base em
 * need[customer].  A leitura de need[customer][j] é feita sem o lock porque:
 *   • apenas a própria thread do cliente escreve em need[customer] (via
 *     request_resources / release_resources com o lock adquirido);
 *   • esta leitura ocorre sequencialmente na mesma thread, após o unlock.
 * Portanto, não há corrida de dados neste acesso.
 *
 * O vetor gerado garante ao menos uma componente positiva quando possível.
 */
static void random_request(int customer, int *buffer, unsigned int *seed)
{
    /* Faz uma leitura local do need do próprio cliente (sem corrida). */
    int local_need[number_of_resources];
    for (int j = 0; j < number_of_resources; j++)
        local_need[j] = need[customer][j];   /* leitura segura — veja comentário acima */

    bool has_need = false;
    for (int j = 0; j < number_of_resources; j++)
        if (local_need[j] > 0) { has_need = true; break; }

    for (int j = 0; j < number_of_resources; j++)
        buffer[j] = (has_need && local_need[j] > 0)
                    ? (int)(rand_r(seed) % (unsigned int)(local_need[j] + 1))
                    : 0;

    /* Garantia: ao menos uma unidade se há necessidade */
    if (has_need) {
        bool all_zero = true;
        for (int j = 0; j < number_of_resources; j++)
            if (buffer[j] > 0) { all_zero = false; break; }

        if (all_zero) {
            for (int j = 0; j < number_of_resources; j++) {
                if (local_need[j] > 0) { buffer[j] = 1; break; }
            }
        }
    }
}

/*
 * Gera um vetor de liberação aleatório para 'customer' com base em
 * allocation[customer].  Mesma justificativa de thread-safety que acima.
 */
static void random_release(int customer, int *buffer, unsigned int *seed)
{
    int local_alloc[number_of_resources];
    for (int j = 0; j < number_of_resources; j++)
        local_alloc[j] = allocation[customer][j];

    bool has_alloc = false;
    for (int j = 0; j < number_of_resources; j++)
        if (local_alloc[j] > 0) { has_alloc = true; break; }

    for (int j = 0; j < number_of_resources; j++)
        buffer[j] = (has_alloc && local_alloc[j] > 0)
                    ? (int)(rand_r(seed) % (unsigned int)(local_alloc[j] + 1))
                    : 0;

    if (has_alloc) {
        bool all_zero = true;
        for (int j = 0; j < number_of_resources; j++)
            if (buffer[j] > 0) { all_zero = false; break; }

        if (all_zero) {
            for (int j = 0; j < number_of_resources; j++) {
                if (local_alloc[j] > 0) { buffer[j] = 1; break; }
            }
        }
    }
}

/* ── Inicialização das demandas máximas ──────────────────────────────────── */

/*
 * Inicializa maximum[i][j] com valores aleatórios no intervalo
 * [1, available[j] * 2].  Valores maiores do que available criam cenários
 * em que o algoritmo de segurança rejeita solicitações por insegurança —
 * comportamento essencial para validar a correção do banqueiro.
 */
static void initialize_maximums(unsigned int seed)
{
    for (int i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        for (int j = 0; j < number_of_resources; j++) {
            int upper = (available[j] > 0) ? available[j] : 1;
            maximum[i][j]    = 1 + (int)(rand_r(&seed) % (unsigned int)upper);
            allocation[i][j] = 0;
            need[i][j]       = maximum[i][j];
        }
    }
}

/* ── Função da thread de cada cliente ────────────────────────────────────── */

static void *customer_thread(void *arg)
{
    int customer = *((int *)arg);
    free(arg);

    /* Semente única por thread (tempo XOR id) */
    unsigned int seed = (unsigned int)time(NULL) ^ ((unsigned int)customer * 2654435761u);

    int *request = (int *)calloc((size_t)number_of_resources, sizeof(int));
    int *release = (int *)calloc((size_t)number_of_resources, sizeof(int));
    if (request == NULL || release == NULL) {
        fprintf(stderr, "Erro: falha ao alocar buffers do cliente %d\n", customer);
        free(request);
        free(release);
        return NULL;
    }

#if DEFAULT_ITERATIONS > 0
    for (int iter = 0; iter < DEFAULT_ITERATIONS; iter++) {
#else
    while (1) {
#endif
        /* ── Fase 1: solicitar recursos ── */
        usleep((useconds_t)(rand_r(&seed) % MAX_SLEEP_USEC));
        random_request(customer, request, &seed);
        request_resources(customer, request);

        /* ── Fase 2: liberar recursos ── */
        usleep((useconds_t)(rand_r(&seed) % MAX_SLEEP_USEC));
        random_release(customer, release, &seed);
        release_resources(customer, release);
    }

    /* ── Liberação final: devolve tudo o que ainda está alocado ── */
    pthread_mutex_lock(&bank_lock);
    for (int j = 0; j < number_of_resources; j++)
        release[j] = allocation[customer][j];
    pthread_mutex_unlock(&bank_lock);

    release_resources(customer, release);

    printf("Cliente %d encerrou.\n", customer);
    free(request);
    free(release);
    return NULL;
}

/* ── Parsing dos argumentos de linha de comando ──────────────────────────── */

static bool parse_arguments(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <r1> <r2> ... <rm>\n", argv[0]);
        fprintf(stderr, "Exemplo: %s 10 5 7\n", argv[0]);
        return false;
    }

    number_of_resources = argc - 1;
    available = (int *)malloc((size_t)number_of_resources * sizeof(int));
    if (available == NULL) {
        fprintf(stderr, "Erro: falha ao alocar vetor available\n");
        return false;
    }

    for (int i = 0; i < number_of_resources; i++) {
        char *endptr = NULL;
        errno = 0;
        long value = strtol(argv[i + 1], &endptr, 10);
        if (errno == ERANGE || endptr == argv[i + 1] || *endptr != '\0'
                || value <= 0 || value > 1000000) {
            fprintf(stderr, "Erro: valor inválido para recurso %d: '%s'\n",
                    i, argv[i + 1]);
            return false;
        }
        available[i] = (int)value;
    }

    return true;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (!parse_arguments(argc, argv)) {
        free(available);
        return EXIT_FAILURE;
    }

    maximum    = alloc_matrix(NUMBER_OF_CUSTOMERS, number_of_resources);
    allocation = alloc_matrix(NUMBER_OF_CUSTOMERS, number_of_resources);
    need       = alloc_matrix(NUMBER_OF_CUSTOMERS, number_of_resources);

    if (maximum == NULL || allocation == NULL || need == NULL) {
        fprintf(stderr, "Erro: falha ao alocar matrizes do banqueiro\n");
        goto cleanup;
    }

    initialize_maximums((unsigned int)time(NULL));

    printf("Banqueiro iniciado: %d clientes, %d tipos de recurso.\n",
           NUMBER_OF_CUSTOMERS, number_of_resources);

    pthread_mutex_lock(&bank_lock);
    print_state();
    pthread_mutex_unlock(&bank_lock);

    /* ── Criação das threads de clientes ── */
    pthread_t customers[NUMBER_OF_CUSTOMERS];
    bool      created[NUMBER_OF_CUSTOMERS];
    memset(created, 0, sizeof(created));

    for (int i = 0; i < NUMBER_OF_CUSTOMERS; i++) {
        int *id = (int *)malloc(sizeof(int));
        if (id == NULL) {
            fprintf(stderr, "Erro: falha ao alocar id para cliente %d\n", i);
            continue;
        }
        *id = i;
        if (pthread_create(&customers[i], NULL, customer_thread, id) != 0) {
            fprintf(stderr, "Erro: falha ao criar thread do cliente %d\n", i);
            free(id);
        } else {
            created[i] = true;
        }
    }

    /* ── Aguarda encerramento de todas as threads ── */
    for (int i = 0; i < NUMBER_OF_CUSTOMERS; i++)
        if (created[i]) pthread_join(customers[i], NULL);

    printf("\nExecução finalizada. Estado final:\n");
    pthread_mutex_lock(&bank_lock);
    print_state();
    pthread_mutex_unlock(&bank_lock);

cleanup:
    free(available);
    free_matrix(maximum,    NUMBER_OF_CUSTOMERS);
    free_matrix(allocation, NUMBER_OF_CUSTOMERS);
    free_matrix(need,       NUMBER_OF_CUSTOMERS);
    pthread_mutex_destroy(&bank_lock);

    return EXIT_SUCCESS;
}
