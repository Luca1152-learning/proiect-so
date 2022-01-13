// Structuri proiect
struct Thread {
    pid_t thread_id;
    LIST_ENTRY(Thread) link;
};

struct Mutex {
    int descriptor;
    bool is_locked;
    bool is_threads_list_init;
    LIST_HEAD(, Thread) waiting_threads_list;
    LIST_ENTRY(Mutex) link;
    pid_t wakeup_tid;
};

struct Process {
    pid_t process_id;
    bool is_mutex_list_init;
    LIST_HEAD(, Mutex) mutex_list;
    LIST_ENTRY(Process) link;
};

// Variabile proiect
LIST_HEAD(, Process) process_list;
bool is_process_list_init;
int resource;

// Logs
const bool OPEN_INFO_LOGS = true;
const bool CLOSE_INFO_LOGS = true;
const bool LOCK_INFO_LOGS = true;
const bool UNLOCK_INFO_LOGS = true;
const bool LIST_INFO_LOGS = true;
const bool GRANT_INFO_LOGS = true;

// 331	STD		{ int sys_mtxopen(void); }
int sys_mtxopen(struct proc *p, void *v, register_t *retval) {
    if (!is_process_list_init) {
        LIST_INIT(&process_list);
        is_process_list_init = true;
    }

    // Cautam procesul cu pid-ul procesului ce apeleaza mtxopen in lista de procese
    struct Process *process = NULL, *process_iterator;
    LIST_FOREACH(process_iterator, &process_list, link){
        // p->p_p->ps_pid = pid-ul procesului
        // Am gasit procesul in lista de procese (exista)
        if (process_iterator->process_id == p->p_p->ps_pid){
            process = process_iterator;
            if (OPEN_INFO_LOGS){
                printf("Info mtxopen(): Am gasit procesul cu pid %d\n", process->process_id);
            }
            break;
        }
    }
    // Nu am gasit procesul in lista de procese, trebuie sa il cream cu valori implicite
    // si sa-l adaugam
    if (process == NULL){
        process = malloc(sizeof(struct Process), M_TEMP, M_WAITOK);
        process->process_id = p->p_p->ps_pid;
        process->is_mutex_list_init = false;
        LIST_INSERT_HEAD(&process_list, process, link);

        if (OPEN_INFO_LOGS){
            printf("Info mtxopen(): Am creat procesul cu pid %d\n", process->process_id);
        }
    }


    // Cream un nou mutex si il adaugam in lista procesului (=> pentru fiecare mtxopen facem un mutex)
    struct Mutex *new_mutex = malloc(sizeof(struct Mutex), M_TEMP, M_WAITOK);
    new_mutex->is_locked = false;

    // Ii setam descriptorul ca [maximul descriptorilor mutecsilor pentru procesul gasit]+1, sa nu existe conflicte
    // Cautam descriptorul maxim:
    int max_mutex_descriptor = 0;
    struct Mutex *mutex_iterator;
    if (process->is_mutex_list_init){
        LIST_FOREACH(mutex_iterator, &(process->mutex_list), link){
            if (mutex_iterator->descriptor > max_mutex_descriptor) {
                max_mutex_descriptor = mutex_iterator->descriptor;
            }
        }
    } else {
        LIST_INIT(&(process->mutex_list));
        process->is_mutex_list_init = true;
    }
    // Setam descriptorul:
    new_mutex->descriptor = max_mutex_descriptor + 1;
    new_mutex->is_threads_list_init = false;

    // Adaugam in lista de mutecsi a procesului
    LIST_INSERT_HEAD(&(process->mutex_list), new_mutex, link);

    printf("Info mtxopen(): Am creat mutexul cu descriptor %d\n", new_mutex->descriptor);

    // Returnam descriptorul (ca sa poata fi folosit in mtxlock si mtxunlock)
    *retval = new_mutex->descriptor;
    return 0;
}

// 332	STD		{ int sys_mtxclose(int d); }
int sys_mtxclose(struct proc *p, void *v, register_t *retval) {
    // Preluam argumentul d (descriptorul mutexului de inchis)
    struct sys_mtxclose_args *uap = v;
    int descriptor = SCARG(uap, d);

    // Lista de procese nu a fost initializata, facem ERRNO -1, deoarece
    // s-a incercat un mtxclose() fara sa se fi facut vreun mtxopen()
    if (!is_process_list_init) {
        printf("Eroare mtxclose(): Nu s-a apelat niciun mtxopen() pentru a face mtxclose()!\n");

        *retval = -1;
        return 0;
    };

    // Cautam procesul curent in lista de procese
    struct Process *process = NULL, *process_iterator;
    LIST_FOREACH(process_iterator, &process_list, link){
        // p->p_p->ps_pid = pid-ul procesului
        if (process_iterator->process_id == p->p_p->ps_pid){
            process = process_iterator;
            break;
        }
    }
    // Nu exista niciun proces in lista de procese cu pid-ul procesului ce a
    // apelat mtxclose => se incearca mtxclose() pe un proces ce nu are niciun mutex
    if (process == NULL){
        printf("Eroare mtxclose(): Nu exista niciun mutex pentru acest proces!\n");

        *retval = -1;
        return 0;
    }


    // Cautam mutex-ul al carui descriptor l-am primit in lista de mutecsi a procesului
    if (!(process->is_mutex_list_init)){
        printf("Eroare mtxclose(): Nu exista niciun mutex pentru acest proces!\n");

        *retval = -1;
        return 0;
    }
    struct Mutex *mutex = NULL, *mutex_iterator;
    //              ^ daca nu pui NULL, stai o ora sa cauti eroarea
    LIST_FOREACH(mutex_iterator, &(process->mutex_list), link){
        if (mutex_iterator->descriptor == descriptor) {
            mutex = mutex_iterator;
        }
    }

    // Nu am gasit mutex-ul cu descriptorul dat => procesul nu are niciun mutex
    // cu descriptorul respectiv
    if (mutex == NULL){
        printf("Eroare mtxclose(): Nu s-a gasit mutex-ul cu descriptorul %d pentru procesul curent!\n", descriptor);

        *retval = -1;
        return 0;
    }

    // Exista mutexul cu descriptorul dat in lista de mutecsi a procesului => il stergem
    LIST_REMOVE(mutex, link);
    if (CLOSE_INFO_LOGS){
        printf("Info mtxclose(): Am gasit mutex-ul cu descriptorul %d si l-am sters\n", descriptor);
    }

    // Daca mutexul sters era, de fapt, singurul mutex al procesului, atunci stergem procesul din lista de procese
    if (LIST_EMPTY(&(process->mutex_list))){
        LIST_REMOVE(process, link);
        if (CLOSE_INFO_LOGS){
            printf("Info mtxclose(): Proces ramas fara mutecsi, a fost sters din lista de procese\n");
        }
    }

    return 0;
}

// 333	STD		{ int sys_mtxlock(int d); }
int sys_mtxlock(struct proc *p, void *v, register_t *retval) {
    // Preluam argumentul d (descriptorul mutexului de blocat)
    struct sys_mtxlock_args *uap = v;
    int descriptor = SCARG(uap, d);

    // Lista de procese nu a fost initializata, facem ERRNO -1, deoarece
    // s-a incercat un mtxlock() fara sa se fi facut vreun mtxopen()
    if (!is_process_list_init) {
        printf("Eroare mtxlock(): Nu s-a apelat niciun mtxopen() pentru a face mtxlock()!\n");

        *retval = -1;
        return 0;
    };

    // Cautam procesul curent in lista de procese
    struct Process *process = NULL, *process_iterator;
    LIST_FOREACH(process_iterator, &process_list, link) {
        // p->p_p->ps_pid = pid-ul procesului
        if (process_iterator->process_id == p->p_p->ps_pid) {
            process = process_iterator;
            break;
        }
    }

    // Nu exista niciun proces in lista de procese cu pid-ul procesului ce a
    // apelat mtxlock => se incearca mtxlock() pe un proces ce nu are niciun mutex
    if (process == NULL) {
        printf("Eroare mtxlock(): Nu exista niciun mutex pentru acest proces!\n");

        *retval = -1;
        return 0;
    }

    // Cautam mutex-ul al carui descriptor l-am primit in lista de mutecsi a procesului
    if (!(process->is_mutex_list_init)) {
        printf("Eroare mtxlock(): Nu exista niciun mutex pentru acest proces!\n");

        *retval = -1;
        return 0;
    }

    struct Mutex *mutex = NULL, *mutex_iterator;
    LIST_FOREACH(mutex_iterator, &(process->mutex_list), link) {
        if (mutex_iterator->descriptor == descriptor) {
            mutex = mutex_iterator;
            break;
        }
    }

    // Nu am gasit mutex-ul cu descriptorul dat => procesul nu are niciun mutex
    // cu descriptorul respectiv
    if (mutex == NULL) {
        printf("Eroare mtxlock(): Nu s-a gasit mutex-ul cu descriptorul %d pentru procesul curent!\n", descriptor);

        *retval = -1;
        return 0;
    }

    // Adaugam thread-ul curent in lista de thread-uri in asteptare a mutexului
    if (!(mutex->is_threads_list_init)) {
        LIST_INIT(&(mutex->waiting_threads_list));
        mutex->is_threads_list_init = true;
    }

    struct Thread *thread = malloc(sizeof(struct Thread), M_TEMP, M_WAITOK);
    thread->thread_id = p->p_tid;
    LIST_INSERT_HEAD(&(mutex->waiting_threads_list), thread, link);
    int error;

    // Punem thread-ul pe sleep pana cand primeste grant
    while (true) {
        error = tsleep(&resource, PSOCK | PCATCH, "awaiting lock", 0);

        if (error) {
            LIST_REMOVE(thread, link);

            *retval = -1;
            return error;
        }

        if (mutex->wakeup_tid == p->p_tid) {
            break;
        }
    }

    LIST_REMOVE(thread, link);

    *retval = 0;
    return 0;
}

// 334	STD		{ int sys_mtxunlock(int d); }
int sys_mtxunlock(struct proc *p, void *v, register_t *retval) {
    // Preluam argumentul d (descriptorul mutexului de deblocat)
    struct sys_mtxunlock_args *uap = v;
    int descriptor = SCARG(uap, d);

    // Lista de procese nu a fost initializata, facem ERRNO -1, deoarece
    // s-a incercat un mtxunlock() fara sa se fi facut vreun mtxopen()
    if (!is_process_list_init) {
        printf("Eroare mtxunlock(): Nu s-a apelat niciun mtxopen() pentru a face mtxunlock()!\n");

        *retval = -1;
        return 0;
    };

    // Cautam procesul curent in lista de procese
    struct Process *process = NULL, *process_iterator;
    LIST_FOREACH(process_iterator, &process_list, link) {
        // p->p_p->ps_pid = pid-ul procesului
        if (process_iterator->process_id == p->p_p->ps_pid) {
            process = process_iterator;
            break;
        }
    }

    // Nu exista niciun proces in lista de procese cu pid-ul procesului ce a
    // apelat mtxunlock => se incearca mtxunlock() pe un proces ce nu are niciun mutex
    if (process == NULL) {
        printf("Eroare mtxunlock(): Nu exista niciun mutex pentru acest proces!\n");

        *retval = -1;
        return 0;
    }

    // Cautam mutex-ul al carui descriptor l-am primit in lista de mutecsi a procesului
    if (!(process->is_mutex_list_init)) {
        printf("Eroare mtxunlock(): Nu exista niciun mutex pentru acest proces!\n");

        *retval = -1;
        return 0;
    }

    struct Mutex *mutex = NULL, *mutex_iterator;
    LIST_FOREACH(mutex_iterator, &(process->mutex_list), link) {
        if (mutex_iterator->descriptor == descriptor) {
            mutex = mutex_iterator;
            break;
        }
    }

    // Nu am gasit mutex-ul cu descriptorul dat => procesul nu are niciun mutex
    // cu descriptorul respectiv
    if (mutex == NULL){
        printf("Eroare mtxunlock(): Nu s-a gasit mutex-ul cu descriptorul %d pentru procesul curent!\n", descriptor);

        *retval = -1;
        return 0;
    }

    mutex->is_locked = false;

    *retval = 0;
    return 0;
}

// 335	STD 	{ int sys_mtxlist(int *processes, int *mutexes, pid_t *tids, size_t lists_size); }
int sys_mtxlist(struct proc *p, void *v, register_t *retval) {
    if (!is_process_list_init) {
        LIST_INIT(&process_list);
        is_process_list_init = true;
    }

    struct sys_mtxlist_args *uap = v;

    int *process_info = SCARG(uap, processes);
    int *mutex_info = SCARG(uap, mutexes);
    int *thread_info = SCARG(uap, tids);
    int max_size = SCARG(uap, lists_size);

    int i = 0;
    struct Process *process_iterator;
    struct Mutex *mutex_iterator;
    struct Thread *thread_iterator;

    LIST_FOREACH(process_iterator, &process_list, link) {
        if (!process_iterator->is_mutex_list_init) {
            LIST_INIT(&process_iterator->mutex_list);
            is_process_list_init = true;
        }

        LIST_FOREACH(mutex_iterator, &process_iterator->mutex_list, link) {
            if (!mutex_iterator->is_threads_list_init) {
                LIST_INIT(&mutex_iterator->waiting_threads_list);
                mutex_iterator->is_threads_list_init = true;
            }

            LIST_FOREACH(thread_iterator, &mutex_iterator->waiting_threads_list, link) {
                if (i < max_size - 1) {
                    process_info[i] = process_iterator->process_id;
                    mutex_info[i] = mutex_iterator->descriptor;
                    thread_info[i] = thread_iterator->thread_id;
                    i++;
                }
            }
        }
    }

    *retval = i;
    return 0;
}

// 336	STD		{ int sys_mtxgrant(int process, int mutex, pid_t tid); }
int sys_mtxgrant(struct proc *p, void *v, register_t *retval) {
    if (!is_process_list_init) {
        printf("Eroare mtxgrant(): Nu s-a creat niciun proces!\n");

        *retval = -1;
        return 0;
    }

    struct sys_mtxgrant_args *uap = v;

    int process_info = SCARG(uap, process);
    int mutex_info = SCARG(uap, mutex);
    pid_t thread_info = SCARG(uap, tid);

    struct Process *process_iterator;
    struct Mutex *mutex_iterator;
    struct Thread *thread_iterator, *thread = NULL;

    LIST_FOREACH(process_iterator, &process_list, link) {
        if (process_iterator->process_id == process_info) {
            if (!process_iterator->is_mutex_list_init) {
                printf("Eroare mtxgrant(): Nu exista niciun mutex pentru acest proces!\n");

                *retval = -1;
                return 0;
            }

            LIST_FOREACH(mutex_iterator, &process_iterator->mutex_list, link) {
                if (mutex_iterator->descriptor == mutex_info) {
                    if (!mutex_iterator->is_threads_list_init) {
                        printf("Eroare mtxgrant(): Nu exista niciun thread in asteptare pentru acest mutex!\n");

                        *retval = -1;
                        return 0;
                    }

                    LIST_FOREACH(thread_iterator, &mutex_iterator->waiting_threads_list, link) {
                        if (thread_iterator->thread_id == thread_info) {
                            thread = thread_iterator;
                            break;
                        }
                    }

                    break;
                }
            }

            break;
        }
    }

    printf("Info mtxgrant(): proces %d, mutex %d, thread %d\n", process_info, mutex_info, thread_info);

    if (thread != NULL) {
        wakeup(&resource);
        mutex_iterator->wakeup_tid = thread->thread_id;
    }

    *retval = 0;
    return 0;
}