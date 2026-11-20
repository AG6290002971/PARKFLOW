/*
 * PARKFLOW - Parking Lot Management System (DSA mini project)
 *
 *   1. STACK    : Lane A and Lane B (array based, 5 spaces each) plus a
 *                 temporary stack used when a vehicle is blocked.
 *   2. QUEUE    : linked-list FIFO queue for vehicles waiting for a space.
 *   3. HASH MAP : separate chaining, key = registration number.
 *
 * Memory rule: every Vehicle is created with malloc() in registerVehicle()
 * and is owned by the hash table. The stacks and the queue only store
 * pointers to it. hashDelete() frees the record, so it is called only after
 * the vehicle has been removed from its lane or from the queue.
 *
 * Build: gcc -std=c11 -Wall -Wextra -Wpedantic -Werror parkflow.c -o parkflow
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NUM_LANES         2
#define CAPACITY          5      /* spaces in one lane */
#define TABLE_SIZE        11     /* small prime, so collisions are easy to see */
#define REG_LEN           15
#define LINE_SIZE         100
#define RATE_PER_HOUR     20     /* INR per started hour */
#define SECONDS_PER_HOUR  3600

/* ------------------------------ Data structures ---------------------------- */

typedef struct {
    int    ticketNo;
    char   regNo[REG_LEN + 1];
    int    lane;        /* 0 = Lane A, 1 = Lane B, -1 while waiting */
    int    parked;      /* 1 = parked in a lane, 0 = waiting in the queue */
    time_t parkTime;    /* time the vehicle entered a lane */
} Vehicle;

/* Stack: array of vehicle pointers. items[top] is the vehicle nearest the gate. */
typedef struct {
    Vehicle *items[CAPACITY];
    int      top;       /* -1 means empty */
} Stack;

/* Queue node (linked list) */
typedef struct QueueNode QueueNode;
struct QueueNode {
    Vehicle   *vehicle;
    QueueNode *next;
};

/* Hash table node (one node in a bucket's chain) */
typedef struct HashNode HashNode;
struct HashNode {
    Vehicle  *vehicle;
    HashNode *next;
};

/* The whole parking lot is kept in global variables to keep the code simple. */
Stack      lanes[NUM_LANES];    /* Lane A = lanes[0], Lane B = lanes[1] */
Stack      tempStack;           /* used only while retrieving a blocked vehicle */
QueueNode *front = NULL;        /* oldest waiting vehicle */
QueueNode *rear  = NULL;        /* newest waiting vehicle */
int        waitingCount = 0;
HashNode  *table[TABLE_SIZE];   /* global, so every bucket starts as NULL */
int        nextTicket = 1001;

/* ---------------------------------- Input ---------------------------------- */

/* Reads one line with fgets(). Returns 0 if the input has ended (EOF). */
int readLine(const char *prompt, char *buf, int size)
{
    int len;
    int ch;

    printf("%s", prompt);
    if (fgets(buf, size, stdin) == NULL) {
        return 0;
    }
    len = (int)strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    } else {
        /* line was longer than the buffer: throw away the rest of it */
        while ((ch = getchar()) != '\n' && ch != EOF) {
        }
    }
    return 1;
}

/*
 * Reads a registration number. Spaces and hyphens are ignored and letters are
 * converted to capital letters, so "wb 02-ab 1234" becomes "WB02AB1234".
 * Returns 1 if the number is valid.
 */
int readRegNo(const char *prompt, char *regNo)
{
    char line[LINE_SIZE];
    int  n = 0;
    int  i;

    if (!readLine(prompt, line, LINE_SIZE)) {
        return 0;
    }
    for (i = 0; line[i] != '\0'; i++) {
        char c = line[i];

        if (c == ' ' || c == '-') {
            continue;
        }
        if (!isalnum((unsigned char)c)) {
            printf("  Invalid registration: use only letters and digits.\n");
            return 0;
        }
        if (n == REG_LEN) {
            printf("  Invalid registration: maximum %d characters.\n", REG_LEN);
            return 0;
        }
        regNo[n++] = (char)toupper((unsigned char)c);
    }
    regNo[n] = '\0';
    if (n < 3) {
        printf("  Invalid registration: minimum 3 characters.\n");
        return 0;
    }
    return 1;
}

/* ----------------------------------- STACK ---------------------------------- */

void initStack(Stack *s)
{
    s->top = -1;
}

int stackIsEmpty(const Stack *s)
{
    return s->top == -1;
}

int stackIsFull(const Stack *s)
{
    return s->top == CAPACITY - 1;
}

/* PUSH: put a vehicle on top of the stack. Returns 0 if the stack is full. */
int push(Stack *s, Vehicle *v)
{
    if (stackIsFull(s)) {
        return 0;
    }
    s->top++;
    s->items[s->top] = v;
    return 1;
}

/* POP: remove and return the top vehicle. Returns NULL if the stack is empty. */
Vehicle *pop(Stack *s)
{
    Vehicle *v;

    if (stackIsEmpty(s)) {
        return NULL;
    }
    v = s->items[s->top];
    s->top--;
    return v;
}

/* Position of v in the stack (0 = bottom), or -1 if it is not there. */
int findPosition(const Stack *s, const Vehicle *v)
{
    int i;

    for (i = 0; i <= s->top; i++) {
        if (s->items[i] == v) {
            return i;
        }
    }
    return -1;
}

/* Prints one lane from the top (nearest the gate) down to slot 1. */
void displayLane(int l)
{
    const Stack *s = &lanes[l];
    int i;

    printf("\n  Lane %c  (%d/%d occupied)\n", 'A' + l, s->top + 1, CAPACITY);
    for (i = CAPACITY - 1; i >= 0; i--) {
        if (i <= s->top) {
            printf("    Slot %d : %-15s PF-%d%s\n", i + 1, s->items[i]->regNo,
                   s->items[i]->ticketNo, (i == s->top) ? "   <-- TOP (next to leave)" : "");
        } else {
            printf("    Slot %d : (empty)\n", i + 1);
        }
    }
}

/* ----------------------------------- QUEUE ---------------------------------- */

/* ENQUEUE: add a vehicle at the rear. Returns 0 if malloc fails. */
int enqueue(Vehicle *v)
{
    QueueNode *node = (QueueNode *)malloc(sizeof(QueueNode));

    if (node == NULL) {
        return 0;
    }
    node->vehicle = v;
    node->next = NULL;

    if (rear == NULL) {         /* queue was empty */
        front = node;
        rear = node;
    } else {
        rear->next = node;
        rear = node;
    }
    waitingCount++;
    return 1;
}

/* DEQUEUE: remove and return the vehicle at the front (NULL if queue is empty). */
Vehicle *dequeue(void)
{
    QueueNode *node;
    Vehicle   *v;

    if (front == NULL) {
        return NULL;
    }
    node = front;
    v = node->vehicle;
    front = front->next;
    if (front == NULL) {        /* queue became empty */
        rear = NULL;
    }
    free(node);
    waitingCount--;
    return v;
}

/* Cancel: remove a vehicle from any position in the queue. Others keep their order. */
int removeFromQueue(const Vehicle *v)
{
    QueueNode *prev = NULL;
    QueueNode *cur = front;

    while (cur != NULL && cur->vehicle != v) {
        prev = cur;
        cur = cur->next;
    }
    if (cur == NULL) {
        return 0;
    }
    if (prev == NULL) {         /* removing the front node */
        front = cur->next;
    } else {
        prev->next = cur->next;
    }
    if (cur == rear) {          /* removing the rear node */
        rear = prev;
    }
    free(cur);
    waitingCount--;
    return 1;
}

void displayQueue(void)
{
    const QueueNode *node;
    int pos = 1;

    printf("\n  WAITING QUEUE (FIFO)\n");
    if (front == NULL) {
        printf("    The queue is empty.\n");
        return;
    }
    for (node = front; node != NULL; node = node->next, pos++) {
        printf("    %d. %-15s PF-%d%s\n", pos, node->vehicle->regNo, node->vehicle->ticketNo,
               (pos == 1) ? "   <-- FRONT (gets the next free space)" : "");
    }
}

/* --------------------------------- HASH MAP --------------------------------- */

/* Hash function: combine all characters of the key, then take the remainder. */
int hashFunction(const char *key)
{
    unsigned int h = 0;

    while (*key != '\0') {
        h = h * 31 + (unsigned char)*key;
        key++;
    }
    return (int)(h % TABLE_SIZE);
}

/*
 * Insertion: find the bucket from the hash of the registration number and add
 * a new node at the head of that bucket's chain. If two keys land in the same
 * bucket (a collision) they simply sit in the same chain.
 */
int hashInsert(Vehicle *v)
{
    int       index = hashFunction(v->regNo);
    HashNode *node = (HashNode *)malloc(sizeof(HashNode));

    if (node == NULL) {
        return 0;
    }
    node->vehicle = v;
    node->next = table[index];
    table[index] = node;
    return 1;
}

/* Search: go to the bucket and walk its chain. Returns NULL if not found. */
Vehicle *hashSearch(const char *regNo)
{
    HashNode *node = table[hashFunction(regNo)];

    while (node != NULL) {
        if (strcmp(node->vehicle->regNo, regNo) == 0) {
            return node->vehicle;
        }
        node = node->next;
    }
    return NULL;
}

/* Deletion: unlink the node from its chain, then free the node AND the vehicle. */
void hashDelete(const char *regNo)
{
    int       index = hashFunction(regNo);
    HashNode *prev = NULL;
    HashNode *cur = table[index];

    while (cur != NULL && strcmp(cur->vehicle->regNo, regNo) != 0) {
        prev = cur;
        cur = cur->next;
    }
    if (cur == NULL) {
        return;
    }
    if (prev == NULL) {         /* node is the first one in the chain */
        table[index] = cur->next;
    } else {
        prev->next = cur->next;
    }
    free(cur->vehicle);
    free(cur);
}

/* --------------------------------- PARKING ---------------------------------- */

/* Returns the less occupied lane that still has space, or -1 if both are full. */
int chooseLane(void)
{
    int i;
    int best = -1;

    for (i = 0; i < NUM_LANES; i++) {
        if (stackIsFull(&lanes[i])) {
            continue;
        }
        if (best == -1 || lanes[i].top < lanes[best].top) {
            best = i;
        }
    }
    return best;
}

/* Parks a vehicle in lane l (the caller has already checked that it has space). */
void parkVehicle(Vehicle *v, int l)
{
    push(&lanes[l], v);
    v->lane = l;
    v->parked = 1;
    v->parkTime = time(NULL);
}

/*
 * BLOCKED VEHICLE RETRIEVAL
 * A vehicle can only leave from the top of its lane. If other vehicles are
 * above it:
 *   1. POP each blocking vehicle from the lane and PUSH it onto tempStack.
 *   2. POP the requested vehicle (it leaves).
 *   3. POP each vehicle from tempStack and PUSH it back onto the lane.
 * The temporary stack reverses the blockers once, and pushing them back
 * reverses them again, so their original order is restored.
 */
int retrieveVehicle(const Vehicle *v)
{
    Stack   *s = &lanes[v->lane];
    Vehicle *x;
    int      pos = findPosition(s, v);

    if (pos == -1) {
        printf("  Error: vehicle not found in its lane.\n");
        return 0;
    }
    initStack(&tempStack);

    while (s->top > pos) {                      /* step 1 */
        x = pop(s);
        push(&tempStack, x);
        printf("  Blocking vehicle %s moved to the temporary stack.\n", x->regNo);
    }

    pop(s);                                     /* step 2 */
    printf("  Vehicle %s removed from Lane %c.\n", v->regNo, 'A' + v->lane);

    while (!stackIsEmpty(&tempStack)) {         /* step 3 */
        x = pop(&tempStack);
        push(s, x);
        printf("  Vehicle %s returned to Lane %c.\n", x->regNo, 'A' + v->lane);
    }
    return 1;
}

/* After a space is released, the vehicle at the front of the queue gets it. */
void promoteWaitingVehicle(void)
{
    int      l = chooseLane();
    Vehicle *v;

    if (waitingCount == 0 || l == -1) {
        return;
    }
    v = dequeue();                              /* oldest waiting vehicle */
    parkVehicle(v, l);
    printf("\n  Waiting vehicle %s (PF-%d) has been moved to Lane %c, Slot %d.\n",
           v->regNo, v->ticketNo, 'A' + l, lanes[l].top + 1);
}

/* ------------------------------- MENU ACTIONS ------------------------------- */

void registerVehicle(void)
{
    char     regNo[REG_LEN + 1];
    Vehicle *v;
    int      l;

    if (!readRegNo("  Enter registration number: ", regNo)) {
        return;
    }
    if (hashSearch(regNo) != NULL) {            /* covers parked AND waiting vehicles */
        printf("  Error: %s is already registered.\n", regNo);
        return;
    }

    v = (Vehicle *)malloc(sizeof(Vehicle));
    if (v == NULL) {
        printf("  Error: out of memory.\n");
        return;
    }
    v->ticketNo = nextTicket;
    strcpy(v->regNo, regNo);
    v->lane = -1;
    v->parked = 0;
    v->parkTime = 0;

    if (!hashInsert(v)) {
        free(v);
        printf("  Error: out of memory.\n");
        return;
    }

    /* A new vehicle must queue if the lot is full OR if others are already waiting. */
    l = chooseLane();
    if (waitingCount > 0 || l == -1) {
        if (!enqueue(v)) {
            hashDelete(regNo);                  /* undo the insertion; frees v */
            printf("  Error: out of memory.\n");
            return;
        }
        nextTicket++;
        printf("  Parking is full. Ticket PF-%d: %s added to the waiting queue (position %d).\n",
               v->ticketNo, v->regNo, waitingCount);
    } else {
        parkVehicle(v, l);
        nextTicket++;
        printf("  Ticket PF-%d: %s parked in Lane %c, Slot %d.\n",
               v->ticketNo, v->regNo, 'A' + l, lanes[l].top + 1);
    }
}

void exitVehicle(void)
{
    char     regNo[REG_LEN + 1];
    Vehicle *v;
    long     seconds;
    long     hours;

    if (!readRegNo("  Enter registration number of the exiting vehicle: ", regNo)) {
        return;
    }
    v = hashSearch(regNo);
    if (v == NULL) {
        printf("  Error: %s was not found.\n", regNo);
        return;
    }
    if (!v->parked) {
        printf("  Error: %s is only waiting in the queue. Use the cancel option instead.\n", regNo);
        return;
    }

    if (!retrieveVehicle(v)) {
        return;
    }
    displayLane(v->lane);

    /* Fee: INR 20 for every started hour (minimum 1 hour). Waiting time is free. */
    seconds = (long)difftime(time(NULL), v->parkTime);
    hours = (seconds + SECONDS_PER_HOUR - 1) / SECONDS_PER_HOUR;
    if (hours < 1) {
        hours = 1;
    }
    printf("\n  Ticket PF-%d | %s | parked %ld min %ld sec | billed hours: %ld\n",
           v->ticketNo, v->regNo, seconds / 60, seconds % 60, hours);
    printf("  PARKING FEE: INR %ld\n", hours * RATE_PER_HOUR);

    hashDelete(regNo);                          /* frees the vehicle record */
    promoteWaitingVehicle();
}

void searchVehicle(void)
{
    char             regNo[REG_LEN + 1];
    const Vehicle   *v;
    const QueueNode *node;
    int              pos;

    if (!readRegNo("  Enter registration number to search: ", regNo)) {
        return;
    }
    v = hashSearch(regNo);
    if (v == NULL) {
        printf("  Vehicle %s not found.\n", regNo);
        return;
    }
    printf("  Found in hash table bucket %d.\n", hashFunction(regNo));
    printf("  Ticket: PF-%d   Registration: %s\n", v->ticketNo, v->regNo);

    if (v->parked) {
        pos = findPosition(&lanes[v->lane], v);
        printf("  Status: PARKED in Lane %c, Slot %d (%d vehicle(s) above it)\n",
               'A' + v->lane, pos + 1, lanes[v->lane].top - pos);
    } else {
        pos = 1;
        for (node = front; node != NULL && node->vehicle != v; node = node->next) {
            pos++;
        }
        printf("  Status: WAITING, queue position %d\n", pos);
    }
}

void displayParkingLot(void)
{
    int i;
    int occupied = 0;

    printf("\n  (Vehicles leave from the TOP of a lane)\n");
    for (i = 0; i < NUM_LANES; i++) {
        displayLane(i);
        occupied += lanes[i].top + 1;
    }
    printf("\n  Occupied: %d/%d   Free: %d   Waiting: %d\n",
           occupied, NUM_LANES * CAPACITY, NUM_LANES * CAPACITY - occupied, waitingCount);
}

void cancelWaitingRequest(void)
{
    char     regNo[REG_LEN + 1];
    Vehicle *v;

    if (!readRegNo("  Enter registration number to cancel: ", regNo)) {
        return;
    }
    v = hashSearch(regNo);
    if (v == NULL) {
        printf("  Error: %s was not found.\n", regNo);
        return;
    }
    if (v->parked) {
        printf("  Error: %s is already parked. Use the exit option instead.\n", regNo);
        return;
    }
    removeFromQueue(v);
    hashDelete(regNo);                          /* frees the vehicle record */
    printf("  Waiting request for %s cancelled.\n", regNo);
}

/* Frees every queue node, hash node and vehicle record. */
void freeAll(void)
{
    int       i;
    HashNode *node;
    HashNode *next;

    while (front != NULL) {
        dequeue();                              /* frees the queue node only */
    }
    for (i = 0; i < TABLE_SIZE; i++) {
        node = table[i];
        while (node != NULL) {
            next = node->next;
            free(node->vehicle);
            free(node);
            node = next;
        }
        table[i] = NULL;
    }
}

/* ----------------------------------- MAIN ----------------------------------- */

int main(void)
{
    char line[LINE_SIZE];
    char *end;
    long choice;
    int  running = 1;
    int  i;

    for (i = 0; i < NUM_LANES; i++) {
        initStack(&lanes[i]);
    }
    initStack(&tempStack);

    printf("=== PARKFLOW: Parking Lot Management System ===\n");
    printf("2 lanes x %d spaces | Rate: INR %d per started hour\n", CAPACITY, RATE_PER_HOUR);

    while (running) {
        printf("\n  1. Register / park a vehicle\n");
        printf("  2. Vehicle exit and fee\n");
        printf("  3. Search a vehicle\n");
        printf("  4. Display parking lanes and occupancy\n");
        printf("  5. Display waiting queue\n");
        printf("  6. Cancel a waiting request\n");
        printf("  7. Exit application\n");

        if (!readLine("  Enter choice (1-7): ", line, LINE_SIZE)) {
            break;                              /* input ended */
        }
        choice = strtol(line, &end, 10);
        if (end == line || *end != '\0') {
            choice = 0;                         /* not a number */
        }

        switch (choice) {
        case 1: registerVehicle();      break;
        case 2: exitVehicle();          break;
        case 3: searchVehicle();        break;
        case 4: displayParkingLot();    break;
        case 5: displayQueue();         break;
        case 6: cancelWaitingRequest(); break;
        case 7: running = 0;            break;
        default: printf("  Invalid choice. Enter a number from 1 to 7.\n");
        }
    }

    freeAll();
    printf("\n  Goodbye!\n");
    return 0;
}