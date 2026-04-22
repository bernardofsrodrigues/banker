# Algoritmo do Banqueiro

Implementação do **Algoritmo do Banqueiro** em **C com pthreads**, desenvolvida como Trabalho Prático 1 da disciplina de Sistemas Operacionais.

---

## Descrição

O programa simula um banco que gerencia múltiplos tipos de recursos compartilhados entre clientes concorrentes. Cada cliente é executado em uma **thread** independente e, em loop, realiza:

1. **Solicitação aleatória** de recursos;
2. **Liberação aleatória** de recursos.

Antes de conceder qualquer solicitação, o banco executa o **algoritmo de segurança**: verifica se — após a concessão — ainda existe uma sequência segura de execução para todos os clientes. Se não existir, a solicitação é **negada** e os recursos **não são alocados**.

---

## Estruturas de dados

| Vetor / Matriz  | Descrição                                              |
|-----------------|--------------------------------------------------------|
| `available`     | Quantidade disponível de cada tipo de recurso          |
| `maximum`       | Demanda máxima declarada por cada cliente              |
| `allocation`    | Recursos atualmente alocados a cada cliente            |
| `need`          | Recursos que ainda faltam para cada cliente            |

A relação invariante mantida é: `need[i][j] = maximum[i][j] − allocation[i][j]`.

---

## Funcionalidades

- Múltiplas threads criadas com **pthreads**
- Controle de acesso via **mutex** (`pthread_mutex_t`) — única seção crítica, sem deadlock
- Verificação de estado seguro (algoritmo de Silberschatz, seção 8.6.3) antes de conceder recursos
- Negação fundamentada: a saída distingue entre *excede need*, *recursos insuficientes* e *estado inseguro*
- Liberação automática de todos os recursos ao término de cada thread
- Exibição do estado completo do sistema após cada operação bem-sucedida

---

## Requisitos

- `gcc` ≥ 7.0  
- `make`  
- Sistema com suporte a **POSIX threads** (Linux / macOS)

Em distribuições Debian/Ubuntu:

```bash
sudo apt install build-essential
```

---

## Arquivos

```
.
├── banker.c    # Código-fonte principal
├── Makefile    # Regras de compilação
└── README.md   # Este arquivo
```

---

## Compilação

```bash
make
```

O executável gerado é `banker`.

Para compilar manualmente (sem Makefile):

```bash
gcc -Wall -Wextra -std=c11 -pthread -O2 -o banker banker.c
```

---

## Execução

Passe o número de instâncias de cada tipo de recurso como argumentos:

```bash
./banker <r1> [r2 ... rn]
```

**Exemplo** (3 tipos de recursos, com 10, 5 e 7 instâncias respectivamente):

```bash
./banker 10 5 7
```

Atalho via Makefile:

```bash
make run
```

---

## Saída esperada

```
Banqueiro iniciado: 5 clientes, 3 tipos de recurso.

=== ESTADO DO SISTEMA ===
Available  = [10 5 7]
Maximum:
  C0: [7 3 9]  C1: [3 2 2]  ...
...

Cliente 2 solicitou [1 0 2] -> CONCEDIDO
...
Cliente 0 solicitou [4 3 1] -> NEGADO (estado inseguro)
...
Cliente 3 liberou    [2 1 0]
...
Execução finalizada. Estado final:
...
```

---

## Configuração

As seguintes constantes em `banker.c` podem ser ajustadas:

| Constante           | Padrão | Descrição                                         |
|---------------------|--------|---------------------------------------------------|
| `NUMBER_OF_CUSTOMERS` | `5`  | Número de clientes (threads)                      |
| `DEFAULT_ITERATIONS`  | `12` | Rodadas por cliente (0 = loop contínuo/infinito)  |
| `MAX_SLEEP_USEC`      | `200000` | Latência máxima entre operações (µs)          |

---

## Limpeza

```bash
make clean
```

---

## Observações sobre concorrência

- Um único `pthread_mutex_t` (`bank_lock`) protege todas as estruturas compartilhadas.  
- Todo output — incluindo o estado do sistema — é impresso **dentro da seção crítica**, garantindo que a saída reflita exatamente o estado após cada operação.  
- Não há possibilidade de **deadlock** entre threads: nenhuma thread mantém o lock ao chamar `request_resources` ou `release_resources` (que adquirem o lock internamente).
