# Trabalho Prático 1 — Algoritmo do Banqueiro

**Disciplina:** Sistemas Operacionais  
**Linguagem:** C (padrão C11) com POSIX Threads (pthreads)  
_Código GitHub:_ https://github.com/bernardofsrodrigues/banker

---

## 1. Introdução

### 1.1 Deadlocks em Sistemas Operacionais

Um dos problemas clássicos de sistemas operacionais multiprogramados é o **deadlock** (impasse): situação em que dois ou mais processos ficam bloqueados indefinidamente, cada um aguardando um recurso mantido por outro processo do grupo. Uma vez em deadlock, nenhum dos processos consegue prosseguir sem intervenção externa, causando travamento parcial ou total do sistema.

Existem quatro condições necessárias e suficientes para a ocorrência de deadlock, conhecidas como as **condições de Coffman** (1971): (i) exclusão mútua, (ii) posse e espera, (iii) não preempção e (iv) espera circular. A presença simultânea dessas quatro condições caracteriza o estado de impasse.

As estratégias de tratamento de deadlocks dividem-se em três categorias: **prevenção** (eliminar estruturalmente ao menos uma condição de Coffman), **evasão** (alocar recursos de forma a nunca entrar em estado inseguro) e **detecção e recuperação** (permitir o deadlock e corrigi-lo posteriormente). A evasão é considerada a abordagem de melhor equilíbrio entre aproveitamento de recursos e garantia de segurança.

### 1.2 O Algoritmo do Banqueiro

O **Algoritmo do Banqueiro**, proposto por Edsger W. Dijkstra em 1965, é o algoritmo de evasão de deadlocks mais estudado na literatura de sistemas operacionais. A metáfora utilizada é a de um banco que concede empréstimos: o banco só libera recursos (crédito) se — após a concessão — ainda for capaz de atender todos os clientes até o término, independentemente da ordem em que façam novas solicitações.

Formalmente, o algoritmo distingue dois conceitos centrais:

- **Estado seguro:** existe ao menos uma sequência de execução (`<P₁, P₂, ..., Pₙ>`) tal que, para todo processo `Pᵢ`, os recursos que ele ainda pode solicitar podem ser satisfeitos com os recursos disponíveis mais os recursos liberados por todos os `Pⱼ` com `j < i`. Um estado seguro garante que o deadlock não ocorrerá.
- **Estado inseguro:** não existe tal sequência. O sistema pode — mas não necessariamente irá — entrar em deadlock.

O algoritmo recusa qualquer solicitação que levaria o sistema a um estado inseguro, mesmo que os recursos solicitados estejam fisicamente disponíveis no momento.

### 1.3 Objetivo do Trabalho

Este trabalho implementa o Algoritmo do Banqueiro em um programa C multithreaded, combinando três tópicos fundamentais de sistemas operacionais:

1. **Criação e gerenciamento de múltiplas threads** (pthreads);
2. **Prevenção de condições de corrida** mediante locks mutex;
3. **Evasão de deadlocks** pelo algoritmo de segurança de Dijkstra.

O programa simula um sistema com cinco clientes concorrentes que solicitam e liberam recursos aleatoriamente de um banco com múltiplos tipos de recursos, cujas quantidades iniciais são fornecidas via linha de comando.

---

## 2. Desenvolvimento

### 2.1 Estruturas de Dados

O banqueiro mantém quatro estruturas de dados globais, todas indexadas por cliente (`i`) e tipo de recurso (`j`):

| Estrutura    | Tipo   | Descrição                                                                    |
| ------------ | ------ | ---------------------------------------------------------------------------- |
| `available`  | vetor  | Quantidade disponível de cada tipo de recurso no momento                     |
| `maximum`    | matriz | Demanda máxima de cada cliente por tipo de recurso                           |
| `allocation` | matriz | Quantidade atualmente alocada a cada cliente                                 |
| `need`       | matriz | Quantidade ainda necessária: `need[i][j] = maximum[i][j] − allocation[i][j]` |

A invariante `need[i][j] = maximum[i][j] − allocation[i][j]` é mantida em todas as operações e serve como verificação implícita da consistência do estado.

Os valores de `available` são inicializados com os argumentos da linha de comando. Os valores de `maximum` são gerados aleatoriamente no intervalo `[1, 2 × available[j]]`, permitindo que clientes declarem uma demanda maior do que os recursos existentes — cenário realista que exercita o algoritmo de segurança em situações de escassez genuína.

### 2.2 Controle de Concorrência

Todo o estado do sistema é compartilhado entre as threads dos clientes. Para evitar condições de corrida, um único mutex POSIX (`bank_lock`) protege exclusivamente todos os acessos de leitura e escrita às estruturas `available`, `maximum`, `allocation` e `need`.

A decisão de usar um único mutex (ao invés de múltiplos locks de granularidade fina) é intencional: o algoritmo de segurança precisa inspecionar _todas_ as estruturas de forma atomicamente consistente. Locks de granularidade fina exigiriam protocolo de ordenação para evitar deadlock entre os próprios locks, adicionando complexidade sem benefício de desempenho significativo neste cenário de número fixo de clientes.

O mutex é adquirido e liberado internamente pelas funções `request_resources` e `release_resources`. As threads de clientes **nunca** adquirem o lock diretamente antes de chamar essas funções, eliminando qualquer risco de deadlock causado por dupla aquisição do mutex (que, sendo do tipo padrão não-reentrante `PTHREAD_MUTEX_INITIALIZER`, resultaria em comportamento indefinido).

### 2.3 Algoritmo de Segurança (`is_safe_state`)

A função `is_safe_state()` implementa o algoritmo de segurança conforme descrito em Silberschatz, Galvin e Gagne (2018), seção 8.6.3:

**Entrada:** estado atual de `available`, `allocation` e `need`.  
**Saída:** `true` se o estado é seguro; `false` caso contrário.

**Algoritmo:**

```
1. work    ← available
   finish  ← [false, false, ..., false]

2. Enquanto houver progresso:
     Para cada cliente i:
       Se finish[i] = false  E  need[i] ≤ work:
         work    ← work + allocation[i]
         finish[i] ← true

3. Se finish[i] = true para todo i: estado seguro
   Caso contrário: estado inseguro
```

A complexidade é O(n² × m), onde `n` é o número de clientes e `m` o número de tipos de recurso. Esta função é sempre chamada com `bank_lock` adquirido, garantindo que o estado inspecionado é consistente.

### 2.4 Função `request_resources`

A função segue estritamente o protocolo do banqueiro:

1. **Validação de `need`:** se `request[j] > need[i][j]` para qualquer `j`, a solicitação é inválida (o cliente está pedindo mais do que declarou precisar) → retorna `-1`.
2. **Disponibilidade:** se `request[j] > available[j]` para qualquer `j`, os recursos são insuficientes → retorna `-1`.
3. **Alocação tentativa:** modifica `available`, `allocation` e `need` como se a solicitação fosse concedida.
4. **Verificação de segurança:** chama `is_safe_state()`.
   - Se seguro: mantém a alocação e retorna `0`.
   - Se inseguro: **desfaz a alocação** (rollback) e retorna `-1`.

Todo o processo ocorre dentro da seção crítica, garantindo atomicidade da operação de tentativa-verificação-confirmação/desfaz.

### 2.5 Função `release_resources`

Após validar que o cliente não está devolvendo mais recursos do que possui alocados, a função:

1. Incrementa `available[j]` com os recursos liberados.
2. Decrementa `allocation[i][j]`.
3. Incrementa `need[i][j]` (o cliente pode solicitar novamente no futuro).

A liberação nunca é negada (desde que válida) e não exige verificação de segurança, pois liberar recursos nunca piora o estado do sistema.

### 2.6 Threads de Clientes

Cada cliente é executado em uma thread independente. Em cada iteração, a thread:

1. **Dorme** por um intervalo aleatório (até `MAX_SLEEP_USEC` microssegundos) — simula processamento entre operações.
2. **Gera um vetor de solicitação aleatório** (`random_request`), com valores entre 0 e `need[i][j]`.
3. **Chama `request_resources`** — o sistema decide concessão ou negação.
4. **Dorme novamente**.
5. **Gera um vetor de liberação aleatório** (`random_release`), com valores entre 0 e `allocation[i][j]`.
6. **Chama `release_resources`**.

Ao encerrar o loop, a thread libera todos os recursos ainda alocados, garantindo que `allocation[i]` retorna a zero antes do encerramento.

As seeds dos geradores aleatórios são individualizadas por thread (`time(NULL) XOR customer_id × constante`), evitando correlação entre as sequências de solicitações.

### 2.7 Saída do Sistema

Para garantir que a saída reflete exatamente o estado resultante de cada operação, toda impressão ocorre **dentro da seção crítica**, imediatamente antes de liberar o mutex. Isso impede que o estado exibido seja alterado por outra thread entre o unlock da operação e o printf.

---

## 3. Resultados

### 3.1 Execução Típica

Para a invocação `./banker 10 5 7`, o programa inicializa o sistema com três tipos de recursos (10, 5 e 7 instâncias, respectivamente) e cinco clientes com demandas máximas geradas aleatoriamente.

**Estado inicial típico:**

```
Banqueiro iniciado: 5 clientes, 3 tipos de recurso.

=== ESTADO DO SISTEMA ===
Available  = [10 5 7]
Maximum:
  C0: [14  6 10]
  C1: [ 3  4  6]
  C2: [18  8 12]
  C3: [ 7  2  4]
  C4: [11  9  6]
Allocation:
  C0: [0 0 0]  C1: [0 0 0]  ...
Need:
  C0: [14  6 10]  C1: [3 4 6]  ...
```

Nota: como `maximum[i][j]` pode ser maior que `available[j]`, desde o início o sistema possui clientes cujas demandas totais excedem a capacidade — cenário que provoca negações por estado inseguro.

### 3.2 Cenário de Concessão (Estado Seguro)

Quando uma solicitação deixa o sistema em estado seguro, ela é concedida:

```
Cliente 1 solicitou [1 0 2] -> CONCEDIDO

=== ESTADO DO SISTEMA ===
Available  = [9 5 5]
Allocation:
  C0: [0 0 0]
  C1: [1 0 2]   ← alocação aumentou
  ...
Need:
  C1: [2 4 4]   ← necessidade diminuiu
```

Após a concessão, o algoritmo de segurança encontrou uma sequência válida de execução — existe ao menos uma ordem em que todos os clientes podem ser atendidos com os recursos restantes.

### 3.3 Cenário de Negação — Recursos Insuficientes

Se os recursos disponíveis forem insuficientes para atender a solicitação, o sistema nega imediatamente sem executar o algoritmo de segurança:

```
Cliente 0 solicitou [8 4 6] -> NEGADO (recursos insuficientes)
```

Esta verificação prévia é uma otimização do algoritmo: evita a alocação tentativa e o rollback quando os recursos são visivelmente insuficientes.

### 3.4 Cenário de Negação — Estado Inseguro

O caso mais interessante é quando os recursos estão disponíveis, mas a concessão levaria o sistema a um estado inseguro:

```
Cliente 3 solicitou [2 1 0] -> NEGADO (estado inseguro)
```

Neste caso, `available` era suficiente para satisfazer `[2 1 0]`, mas após a alocação tentativa o algoritmo de segurança não encontrou nenhuma sequência de execução viável — algum cliente ficaria bloqueado sem possibilidade de completar, caracterizando risco de deadlock.

### 3.5 Análise de Concorrência

Durante a execução, múltiplas threads competem pelo `bank_lock`. O padrão de output reflete essa concorrência: solicitações de diferentes clientes se intercalam, mas cada operação atômica é sempre exibida com o estado interno consistente. Não foram observados deadlocks, estados inconsistentes ou erros de segmentação em múltiplas execuções com diferentes tamanhos de recursos.

### 3.6 Estado Final

Ao término de todas as threads, o estado final mostra todas as alocações zeradas e `available` igual à soma inicial de todos os recursos — confirmando que não houve vazamento de recursos:

```
Execução finalizada. Estado final:

=== ESTADO DO SISTEMA ===
Available  = [10 5 7]   ← idêntico ao estado inicial
Allocation:
  C0: [0 0 0]  C1: [0 0 0]  ...
Need = Maximum (todos retornaram ao estado inicial)
```

---

## 4. Conclusão

### 4.1 Avaliação do Funcionamento

A implementação atende a todos os requisitos do enunciado: múltiplas threads concorrentes, controle de acesso via mutex, verificação de estado seguro antes de cada concessão e negação de solicitações que deixariam o sistema em estado inseguro. A validade da implementação é evidenciada pela corretude do estado final — todos os recursos são devolvidos ao sistema, sem perdas nem duplicações.

### 4.2 Importância do Algoritmo

O Algoritmo do Banqueiro ilustra um princípio fundamental da engenharia de sistemas: é possível garantir propriedades de segurança em sistemas concorrentes mediante análise do espaço de estados futuro. Em vez de reagir ao deadlock após sua ocorrência, o banqueiro raciocina proativamente sobre as consequências de cada decisão de alocação.

O algoritmo tem limitações práticas reconhecidas na literatura: requer que cada cliente declare previamente sua demanda máxima, que o número de recursos e processos seja fixo, e sua complexidade O(n²m) pode ser proibitiva para sistemas com milhares de processos. Por essas razões, sistemas operacionais modernos geralmente optam por estratégias de prevenção (eliminar posse-e-espera ou não-preempção) em vez de evasão. Ainda assim, o banqueiro permanece o modelo pedagógico mais claro para ensinar o conceito de evasão de deadlocks.

### 4.3 Aprendizados

O desenvolvimento deste trabalho consolidou a compreensão prática de três pilares de sistemas operacionais:

- **Programação concorrente:** a criação de threads com pthreads e a sincronização via mutex demonstram como o acesso a dados compartilhados deve ser rigorosamente controlado. Um único acesso não protegido pode introduzir condições de corrida de difícil depuração.

- **Raciocínio sobre segurança:** a distinção entre estado seguro e inseguro, e a técnica de alocação tentativa seguida de rollback, demonstram como verificar invariantes de segurança sem modificar permanentemente o estado do sistema.

- **Design de seções críticas:** a decisão de manter toda a lógica — incluindo a impressão de resultados — dentro da seção crítica simplifica o raciocínio sobre consistência e elimina uma classe inteira de bugs de output concorrente.

---

## Referências

- SILBERSCHATZ, A.; GALVIN, P. B.; GAGNE, G. **Fundamentos de Sistemas Operacionais**. 9. ed. Rio de Janeiro: LTC, 2015. Cap. 8 (Deadlocks).
- DIJKSTRA, E. W. **Co-operating Sequential Processes**. In: GENUYS, F. (Ed.). _Programming Languages_. Academic Press, 1968.
- IEEE. **POSIX.1-2017 — IEEE Standard for Information Technology: Portable Operating System Interface (POSIX)**. IEEE, 2018.
