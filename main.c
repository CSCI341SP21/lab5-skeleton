#include "lab5.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define LOCK(lock) do { \
  if (pthread_mutex_lock((lock)) < 0) { \
    fprintf(stderr, "%s:%d %s: %s\n", __FILE__, __LINE__, __FUNCTION__, strerror(errno)); \
    exit(2); \
  } \
} while (false)

#define UNLOCK(lock) do { \
  if (pthread_mutex_unlock((lock)) < 0) { \
    fprintf(stderr, "%s:%d %s: %s\n", __FILE__, __LINE__, __FUNCTION__, strerror(errno)); \
    exit(2); \
  } \
} while (false)

// when stop == true, all threads quit voluntarily
static volatile atomic_bool stop;

/* This is an internal struct used by the enforcement system
   - there is no access to this from hw6.c */
static struct Elevator {
  pthread_mutex_t lock;
  int seqno, last_action_seqno; // these two are used to enforce control rules
  int floor;
  int open;
  int passengers;
  int trips;
} elevators[ELEVATORS];

static void elevator_check(int elevator) {
  /*
     if(elevators[elevator].seqno == elevators[elevator].last_action_seqno) {
     log(0,"VIOLATION: elevator %d make at most one action call per
     elevator_ready()\n",elevator); exit(1);
     }*/
  if (elevators[elevator].passengers > MAX_CAPACITY ||
      elevators[elevator].passengers < 0) {
    log(0,
        "VIOLATION: elevator %d over capacity, or negative passenger count "
        "%d!\n",
        elevator, elevators[elevator].passengers);
    exit(1);
  }
  elevators[elevator].last_action_seqno = elevators[elevator].seqno;
}

static void elevator_move_direction(int elevator, int direction) {
  LOCK(&elevators[elevator].lock);

  elevator_check(elevator);
  log(8, "Moving elevator %d %s from %d\n", elevator,
      (direction == -1 ? "down" : "up"), elevators[elevator].floor);
  if (elevators[elevator].open) {
    log(0, "VIOLATION: attempted to move elevator %d with door open.\n",
        elevator);
    exit(1);
  }
  if (elevators[elevator].floor >= FLOORS || elevators[elevator].floor < 0) {
    log(0, "VIOLATION: attempted to move elevator %d outside of building!\n",
        elevator);
    exit(1);
  }

  UNLOCK(&elevators[elevator].lock);
  sched_yield();
  usleep(DELAY);

  LOCK(&elevators[elevator].lock);
  elevators[elevator].floor += direction;
  UNLOCK(&elevators[elevator].lock);
}

static void elevator_open_door(int elevator) {
  LOCK(&elevators[elevator].lock);

  elevator_check(elevator);
  log(9, "Opening elevator %d at floor %d\n", elevator,
      elevators[elevator].floor);
  if (elevators[elevator].open) {
    log(0, "VIOLATION: attempted to open elevator %d door when already open.\n",
        elevator);
    exit(1);
  }
  elevators[elevator].open = 1;

  UNLOCK(&elevators[elevator].lock);

  usleep(10 * DELAY);
}

static void elevator_close_door(int elevator) {
  LOCK(&elevators[elevator].lock);

  elevator_check(elevator);
  log(9, "Closing elevator %d at floor %d\n", elevator,
      elevators[elevator].floor);
  if (!elevators[elevator].open) {
    log(0,
        "VIOLATION: attempted to close elevator %d door when already closed.\n",
        elevator);
    exit(1);
  }

  UNLOCK(&elevators[elevator].lock);

  sched_yield();

  usleep(10 * DELAY);
  LOCK(&elevators[elevator].lock);
  elevators[elevator].open = 0;
  UNLOCK(&elevators[elevator].lock);
}

static void *start_elevator(void *arg) {
  size_t elevator = (size_t)arg;
  log(6, "Starting elevator %lu\n", elevator);

  struct Elevator *e = &elevators[elevator];

  LOCK(&e->lock);
  e->last_action_seqno = 0;
  e->seqno = 1;
  e->passengers = 0;
  e->trips = 0;
  e->floor = 0;
  UNLOCK(&e->lock);

  while (!atomic_load(&stop)) {
    LOCK(&e->lock);
    e->seqno++;
    int floor = e->floor;
    UNLOCK(&e->lock);
    elevator_ready(elevator, floor, elevator_move_direction,
                   elevator_open_door, elevator_close_door);
    sched_yield();
  }

  return NULL;
}

/* This is an internal struct used by the enforcement system
   - there is no access to this from hw6.c. */

static struct Passenger {
  pthread_mutex_t lock;
  int id;
  int from_floor;
  int to_floor;
  int in_elevator;
  enum { WAITING, ENTERED, EXITED } state;
} passengers[PASSENGERS];

static void passenger_enter(int passenger, int elevator) {
  // Lock passenger first.
  LOCK(&passengers[passenger].lock);
  LOCK(&elevators[elevator].lock);

  if (passengers[passenger].from_floor != elevators[elevator].floor) {
    log(0, "VIOLATION: let passenger %d on on wrong floor %d!=%d.\n",
        passengers[passenger].id, passengers[passenger].from_floor,
        elevators[elevator].floor);
    exit(1);
  }
  if (!elevators[elevator].open) {
    log(0,
        "VIOLATION: passenger %d walked into a closed door entering elevator "
        "%d.\n",
        passengers[passenger].id, elevator);
    exit(1);
  }
  if (elevators[elevator].passengers == MAX_CAPACITY) {
    log(0, "VIOLATION: passenger %d attempted to board full elevator %d.\n",
        passengers[passenger].id, elevator);
    exit(1);
  }
  if (passengers[passenger].state != WAITING) {
    log(0,
        "VIOLATION: passenger %d told to board elevator %d, was not waiting.\n",
        passengers[passenger].id, elevator);
    exit(1);
  }

  log(6, "Passenger %d got on elevator %d at %d, requested %d\n",
      passengers[passenger].id, elevator, passengers[passenger].from_floor,
      elevators[elevator].floor);
  elevators[elevator].passengers++;
  passengers[passenger].in_elevator = elevator;
  passengers[passenger].state = ENTERED;

  // Unlock elevator first.
  UNLOCK(&elevators[elevator].lock);
  UNLOCK(&passengers[passenger].lock);
  sched_yield();
  usleep(DELAY);
}

static void passenger_exit(int passenger, int elevator) {
  // Lock passenger first.
  LOCK(&passengers[passenger].lock);
  LOCK(&elevators[elevator].lock);

  if (passengers[passenger].to_floor != elevators[elevator].floor) {
    log(0, "VIOLATION: let passenger %d off on wrong floor %d!=%d.\n",
        passengers[passenger].id, passengers[passenger].to_floor,
        elevators[elevator].floor);
    exit(1);
  }
  if (!elevators[elevator].open) {
    log(0,
        "VIOLATION: passenger %d walked into a closed door leaving elevator "
        "%d.\n",
        passengers[passenger].id, elevator);
    exit(1);
  }
  if (passengers[passenger].state != ENTERED) {
    log(0,
        "VIOLATION: passenger %d told to board elevator %d, was not waiting.\n",
        passengers[passenger].id, elevator);
    exit(1);
  }

  log(6, "Passenger %d got off elevator %d at %d, requested %d\n",
      passengers[passenger].id, elevator, passengers[passenger].to_floor,
      elevators[elevator].floor);
  elevators[elevator].passengers--;
  elevators[elevator].trips++;
  passengers[passenger].in_elevator = -1;
  passengers[passenger].state = EXITED;

  // Unlock elevator first.
  UNLOCK(&elevators[elevator].lock);
  UNLOCK(&passengers[passenger].lock);

  sched_yield();
  usleep(DELAY);
}

static void *start_passenger(void *arg) {
  size_t passenger = (size_t)arg;
  struct Passenger *p = &passengers[passenger];
  log(6, "Starting passenger %lu\n", passenger);
  LOCK(&p->lock);
  p->from_floor = random() % FLOORS;
  p->in_elevator = -1;
  p->id = passenger;
  UNLOCK(&p->lock);
  int trips = TRIPS_PER_PASSENGER;
  while (!atomic_load(&stop) && trips-- > 0) {
    int to_floor = random() % FLOORS;
    LOCK(&p->lock);
    int from_floor = p->from_floor;
    p->to_floor = to_floor;
    passengers[passenger].state = WAITING;
    UNLOCK(&p->lock);

    log(6, "Passenger %lu requesting %d->%d\n", passenger, from_floor,
        to_floor);

    struct timeval before;
    gettimeofday(&before, 0);
    passenger_request(passenger, p->from_floor, p->to_floor, passenger_enter,
                      passenger_exit);
    struct timeval after;
    gettimeofday(&after, 0);
    int ms = (after.tv_sec - before.tv_sec) * 1000 +
             (after.tv_usec - before.tv_usec) / 1000;
    log(1, "Passenger %lu trip duration %d ms, %d slots\n", passenger, ms,
        ms * 1000 / DELAY);

    LOCK(&p->lock);
    p->from_floor = p->to_floor;
    UNLOCK(&p->lock);
    usleep(100);
  }
  return NULL;
}

static void *draw_state(void *ptr) {
  while (!atomic_load(&stop)) {
    printf("\033[2J\033[1;1H");
    for (int floor = FLOORS - 1; floor >= 0; floor--) {
      printf("%d\t", floor);
      for (int el = 0; el < ELEVATORS; el++) {
        LOCK(&elevators[el].lock);
        if (elevators[el].floor == floor)
          printf(" %c ", (elevators[el].open ? 'O' : '_'));
        else
          printf(" %c ", elevators[el].floor > floor ? '|' : ' ');
        UNLOCK(&elevators[el].lock);
      }
      printf("    ");
      int align = 5 * ELEVATORS;
      for (int p = 0; p < PASSENGERS; p++) {
        bool at_floor = false;

        LOCK(&passengers[p].lock);
        if (passengers[p].state == ENTERED) {
          LOCK(&elevators[passengers[p].in_elevator].lock);
          at_floor = elevators[passengers[p].in_elevator].floor == floor;
          UNLOCK(&elevators[passengers[p].in_elevator].lock);
        }
        if (at_floor) {
          align -= 5;
          printf("->%02d ", passengers[p].to_floor);
        }
      }
      while (align-- > 0)
        printf(" ");
      printf("X ");
      for (int p = 0; p < PASSENGERS; p++) {
        LOCK(&passengers[p].lock);
        if ((passengers[p].from_floor == floor &&
             passengers[p].state == WAITING)) {
          printf("->%d ", passengers[p].to_floor);
        }
        UNLOCK(&passengers[p].lock);
      }
      printf("\n");
    }
    fflush(stdout);
    usleep(DELAY);
  }
  return NULL;
}

int main(int argc, char **argv) {
  pthread_mutexattr_t mutex_attr;
  if (pthread_mutexattr_init(&mutex_attr)) {
    perror("pthread_mutexattr_init");
    return 1;
  }
  if (pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ERRORCHECK)) {
    perror("pthread_mutexattr_settype(PTHREAD_MUTEX_ERRORCHECK)");
    return 1;
  }

  atomic_init(&stop, false);
  struct timeval before;
  gettimeofday(&before, 0);

  scheduler_init();

  pthread_t passenger_threads[PASSENGERS];
  for (size_t i = 0; i < PASSENGERS; i++) {
    if (pthread_mutex_init(&passengers[i].lock, &mutex_attr)) {
      perror("pthread_mutex_init()");
      return 1;
    }
    pthread_create(&passenger_threads[i], NULL, start_passenger, (void *)i);
  }
  usleep(100000);

  pthread_t elevator_threads[ELEVATORS];
  for (size_t i = 0; i < ELEVATORS; i++) {
    if (pthread_mutex_init(&elevators[i].lock, &mutex_attr)) {
      perror("pthread_mutex_init()");
      return 1;
    }
    pthread_create(&elevator_threads[i], NULL, start_elevator, (void *)i);
  }

#ifndef NODISPLAY
  pthread_t draw;
  pthread_create(&draw, NULL, draw_state, NULL);
#endif

  /* wait for all trips to complete */
  for (int i = 0; i < PASSENGERS; i++)
    pthread_join(passenger_threads[i], NULL);
  atomic_store(&stop, true);
  for (int i = 0; i < ELEVATORS; i++)
    pthread_join(elevator_threads[i], NULL);
  
#ifndef NODISPLAY
  pthread_join(draw, NULL);
#endif

  struct timeval after;
  gettimeofday(&after, 0);

  log(0, "All %d passengers finished their %d trips each.\n", PASSENGERS,
      TRIPS_PER_PASSENGER);
  int ms = (after.tv_sec - before.tv_sec) * 1000 +
           (after.tv_usec - before.tv_usec) / 1000;
  log(0, "Total time elapsed: %d ms, %d slots\n", ms, ms * 1000 / DELAY);

  return 0;
}
