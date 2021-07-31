mutex mtx;
condvar prod,cons;

//Contador de productores
int prod_count = 0;
//Contador de consumidores
int cons_count = 0;
//Comida
struct kfifo cbuffer;

void fifoproc_open(bool abre_para_lectura) {
    lock(mtx);
    
    /**
     Si es un consumidor:
        Incrementamos la variable de consumidores.
        Avisamos a los productores.
     Si es un productor:
        Incrementamos la variable de productores.
        Avisamos a los consumidores por si estuviesen esperando a un productor.
     */
    if (abre_para_lectura) {
        cons_count++;
        cond_signal(cons);
        while (prod_count == 0) {
            cond_wait(prod, mtx); //¿Hacer broadcast mejor?
        }
    } else {
        prod_count++;
        cond_signal(prod);
        while (cons_count == 0) {
            cond_wait(cons, mtx);
        }
    }
    
    unlock(mtx);
}

int fifoproc_write(char* buff, int len) {
    char kbuffer[MAX_KBUF];
    
    if (len > MAX_CBUFFER_LEN || len > MAX_KBUF) {
        return Error;
    }
    
    if (copy_from_user(kbuffer, buff, len)) {
        return Error;
    }
    
    lock(mtx);
    
    /* Esperar hasta que haya hueco para insertar (debe haber consumidores)
     1º- Comprobamos si hay suficiente hueco para la cadena de tamaño 'len' (kfifo_avail).
     2º- También comprobamos si hay consumidores en caso nos bloqueamos.
     Si se cumplen las 2 condiciones nos bloqueamos.
     */
    while (kfifo_avail(&cbuffer) < len && cons_count > 0){
        cond_wait(prod, mtx);
    }
    
    //Si no hay consumidores, liberamos mutex y devolvemos error.
    if (cons_count == 0) {
        unlock(mtx);
        return -EPIPE; /* Broken pipe */
    }
    
    //Introducimos datos
    kfifo_in(&cbuffer, kbuffer,len);
    
    /* Despertar a posible consumidor bloqueado */
    cond_signal(cons);
    
    unlock(mtx);
    
    return len;
}

int fifoproc_read(const char* buff, int len) {
    char kbuffer[MAX_KBUF];
    
    if (len> MAX_CBUFFER_LEN || len> MAX_KBUF) {
        return Error;
    }
    
    lock(mtx);
    
    /*
     Nos bloqueamos si:
     1º Si no están todos los datos que queremos leer, es decir,
     en 'cbuffer' es menor que el número de elementos a leer que es 'len'.
     */
    while (kfifo_len(&cbuffer) < len && prod_count > 0){
        cond_wait(cons, mtx); //Nos bloqueamos
    }
    
    //Si no hay productores, liberamos mutex y devolvemos error.
    if (prod_count == 0 && kfifo_is_empty()) {
        unlock(mtx);
        return 0; /* Broken pipe */
    }
    
    kfifo_out(&cbuffer, kbuffer, len); //Consumimos
    
    // Transfer data from the kernel to userspace
    if (copy_to_user(buff, kbuffer, len))
    {
        return -EINVAL;
    }
    
    /* Despertar a todos los productores bloqueados */
    cond_broadcast(prod);
    
    unlock(mtx);
    
    return len;
}

void fifoproc_release(bool lectura) {
    lock(mtx);
    
     /**
        Si es un consumidor:
           Decrementamos la variable de consumidores.
           Avisamos a los productores.
        Si es un productor:
           Decrementamos la variable de productores.
           Avisamos a los consumidores por si estuviesen.
        */
    if (lectura) {
        cons_count--;
        cond_signal(prod);
    } else {
        prod_count--;
        cond_signal(cons);
    }
    
    //Si uno de los 2 ha llegado a 0, se rompe la "tubería".
    if (cons_count == 0 && prod_count == 0) {
        kfifo_reset(&cbuffer);
    }
    
    unlock(mtx);
}
