/*
* Archivo: servidor.c
* Autor: Álvaro Goldar Dieste
* Fecha de creación: 23/11/2018 - 12:45
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>


#define LEN 1024
#define MAX_SOLICITUDES_COLA 10

struct infoHilo
{
    // Identificador del socket de conexión
    int socketConexion;

    // TID
    pthread_t numeroHilo;

    // Dirección para un socket
    struct sockaddr_in direccion;
    // Tamaño de la dirección
    socklen_t tamanoDireccion;

    // Puntero al número de hilos activos
    int *hilosActivos;

    // Puntero al mutex
    pthread_mutex_t *mutex;
};


struct infoFichero
{
    // Si el fichero existe
    uint8_t existe;

    // Contenido del fichero
    char *contenido;
    // Tamaño del fichero en bytes
    int numeroBytes;

    // Información del fichero
    struct stat estado;
};


struct peticion
{
    // Comando de la petición
    char *comando;
    // Ruta de la petición
    char *ruta;
    // Protocolo de la petición
    char *protocolo;

    // Si el formato de la petición es correcto
    uint8_t peticionCorrecta;

    // Si se decide mantener abierta la conexión
    uint8_t keepAlive;

    // Tipo de contenido del fichero
    char tipoContenido[ 11 ];

    // Fichero
    struct infoFichero fichero;

    // Respuesta generada a la petición
    char *respuesta;
    // Tamaño de la respuesta
    size_t tamanoRespuesta;
};




int servidor( char *puertoArgumento );


int main( int argc, char **argv ) {

    if( argc < 2 )
    {
        perror( "No se ha especificado el puerto de escucha" );
        exit( EXIT_FAILURE );
    }

    servidor( argv[ 1 ] );

}


void descomponerMensaje( char *mensaje, struct peticion *peticionHTTP )
{
    // Posiciones de cada parámetro en el mensaje
    char *posicionComando = mensaje;
    char *posicionRuta = NULL;
    char *posicionProtocolo = NULL;
    char *posicionSiguienteLinea = NULL;
    char *posicionKeepAlive = NULL;


    // Se extraen los campos de uno en uno; para ello, se necesita la posición
    // de inicio de uno y la posición de fin del siguiente

    // Si no se ha transmitido nada
    if( posicionComando == NULL )
    {
        perror( "Comando no dado" );
        peticionHTTP->peticionCorrecta = 0;
        return;
    }

    // Se obtiene la posición en la que comienza la ruta
    posicionRuta = posicionComando;
    strsep( &posicionRuta, " " );

    // Si no se ha encontrado dicha posicion
    if( posicionRuta == NULL )
    {
        perror( "Ruta del archivo no dada" );
        peticionHTTP->peticionCorrecta = 0;
        return;
    }

    // Se guarda el comando mediante la posición de inicio de este y la
    // posición de inicio del siguiente parámetro
    if( ( peticionHTTP->comando = ( char * )malloc( ( posicionRuta -
        posicionComando ) * sizeof( char ) ) ) == NULL )
    {
        perror( "Reserva de memoria fallida\n" );
        exit( EXIT_FAILURE );
    }

    strncpy( peticionHTTP->comando, posicionComando, posicionRuta -
        posicionComando );

    // Se obtiene la posición en la que comienza el protocolo
    posicionProtocolo = posicionRuta;
    strsep( &posicionProtocolo, " " );

    // Si no se ha encontrado dicha posicion
    if( posicionProtocolo == NULL )
    {
        perror( "Protocolo no dado" );
        peticionHTTP->peticionCorrecta = 0;
        return;
    }

    // Se guarda la ruta mediante la posición de inicio de este y la posición
    // de inicio del siguiente parámetro

    // Deben reservarse al menos 11 espacios por si se debe indicar index.html
    if( ( peticionHTTP->ruta = ( char * )malloc( ( ( posicionProtocolo -
        posicionRuta < 11 ) ? 11 : posicionProtocolo - posicionRuta ) *
        sizeof( char ) ) ) == NULL )
    {
        perror( "Reserva de memoria fallida\n" );
        exit( EXIT_FAILURE );
    }

    // Se ignora el primer carácter ('/')
    strncpy( peticionHTTP->ruta, posicionRuta + 1, posicionProtocolo -
        posicionRuta - 1 );

    // Si la ruta está vacía, se sustituye por index.html
    if( strcmp( peticionHTTP->ruta, "" ) == 0 )
        strcpy( peticionHTTP->ruta, "index.html" );

    // Se obtiene la posición en la que comienza la nueva línea ('\r')
    posicionSiguienteLinea = posicionProtocolo;
    strsep( &posicionSiguienteLinea, "\r" );

    // Si no se ha encontrado dicha posicion
    if( posicionSiguienteLinea == NULL )
    {
        perror( "Formato de la petición incorrecto" );
        peticionHTTP->peticionCorrecta = 0;
        return;
    }

    // Se guarda el protocolo mediante la posición de inicio de este y la
    // posición de inicio del siguiente parámetro
    if( ( peticionHTTP->protocolo = ( char * )malloc( ( posicionSiguienteLinea
        - posicionProtocolo ) * sizeof( char ) ) ) == NULL )
    {
        perror( "Reserva de memoria fallida\n" );
        exit( EXIT_FAILURE );
    }
    strncpy( peticionHTTP->protocolo, posicionProtocolo,
        posicionSiguienteLinea - posicionProtocolo );

    // Ahora, se avanza al inicio de cada línea hasta encontrar aquella que
    // comience por una 'C'
    posicionKeepAlive = posicionSiguienteLinea;
    while( posicionKeepAlive != NULL && posicionKeepAlive[ 0 ] != 'C' )
    {
        strsep( &posicionKeepAlive, "\n" );
    }

    // Si se ha encontrado el especificador del cierre de conexión
    if( posicionKeepAlive != NULL )
    {
        // Se avanza hasta después de ": "
        strsep( &posicionKeepAlive, " " );

        // Si se indica un cierre de la conexión
        if( posicionKeepAlive[ 0 ] == 'c' )
            peticionHTTP->keepAlive = 0;
    }
    // En caso contrario, seguirá asumiéndose una conexión persistente
}


void determinarContenido( struct peticion *peticionHTTP )
{
    // Buffer
    char buffer[ strlen( peticionHTTP->ruta ) + 1 ];
    // Puntero auxiliar
    char *aux = NULL;


    // Se copia al buffer el contenido de ruta de la petición para no alterarlo
    strcpy( buffer, peticionHTTP->ruta );

    // Se apunta al buffer y se busca un '.'
    aux = buffer;
    strsep( &aux, "." );

    // Si el archivo solicitado no presenta una extensión, no existirá, por lo
    // cual no se determina el contenido de la respuesta
    if( aux == NULL )
        return;

    switch( aux[ 0 ] ) {

        case 'a' :
            strcpy( peticionHTTP->tipoContenido, "audio/aac" );
            break;

        case 'g' :
            strcpy( peticionHTTP->tipoContenido, "image/gif" );
            break;

        case 'h':
            strcpy( peticionHTTP->tipoContenido, "text/html" );
            break;

        case 'i' :
            strcpy( peticionHTTP->tipoContenido, "image/x-icon" );
            break;

        case 'j' :
            strcpy( peticionHTTP->tipoContenido, "image/jpeg" );
            break;

        case 'p' :
            strcpy( peticionHTTP->tipoContenido, "image/png" );
            break;

        case 'w' :
            strcpy( peticionHTTP->tipoContenido, "video/wemb");
            break;

        default:
            strcpy( peticionHTTP->tipoContenido, "text/html" );
            break;
    }
}


void cargarFichero( struct peticion *peticionHTTP )
{
    // Archivo a leer
    int fichero = 0;


    // Si el fichero pedido no existe
    if( ( fichero = open( peticionHTTP->ruta, O_RDONLY ) ) < 0 )
    {
        perror( "El archivo no existe\n" );
        peticionHTTP->fichero.existe = 0;
        return;
    }

    // Se leen datos del fichero, como la última fecha de modificación
    if( fstat( fichero, &( peticionHTTP->fichero.estado ) ) < 0 )
    {
        perror( "Lectura de propiedades del archivo fallida\n" );
        exit( EXIT_FAILURE );
    }

    // Se lee el tamaño del fichero
    peticionHTTP->fichero.numeroBytes = peticionHTTP->fichero.estado.st_size;

    // Se reserva memoria para cargar el fichero en memoria
    peticionHTTP->fichero.contenido = ( char * )malloc( (
        peticionHTTP->fichero.numeroBytes + 1 ) * sizeof( char ) );
    // Y se carga en memoria
    if( ( read( fichero, peticionHTTP->fichero.contenido,
        peticionHTTP->fichero.numeroBytes ) ) < 0 )
    {
        perror( "Lectura del fichero fallida\n" );
        exit( EXIT_FAILURE );
    }

    // Se finaliza la lectura del fichero
    close( fichero );
}


void componerRespuesta( struct peticion *peticionHTTP )
{
    // Buffer en el que escribir la respuesta
    char *buffer = NULL;
    // Puntero empleado para escribir de línea en línea en el buffer
    char *posicion = NULL;
    // Tamaño a reservar para la respuesta
    int tamano = 0;

    // Variable para obtener la fecha local
    time_t fecha;

    // Se añade el tamaño establecido para recibir peticiones
    tamano = LEN;
    // Y, en caso de tener que responder con el contenido de un fichero, se
    // añade el tamaño de este
    if( peticionHTTP->fichero.existe )
        tamano += peticionHTTP->fichero.numeroBytes;

    if( ( buffer = ( char * )malloc( tamano * sizeof( char ) ) ) ==  NULL )
    {
        perror( "Reserva de memoria fallida\n" );
        exit( EXIT_FAILURE );
    }
    posicion = buffer;

    // Al aumentar el puntero en el número de bytes escritos exceptuando el
    // '\0', se sitúa justo en su posición para sobreescribirlo en posteriores
    // lecturas

    /* Línea inicial */
    posicion += sprintf( posicion, "%s %s\r\n", peticionHTTP->protocolo,
            ( !peticionHTTP->peticionCorrecta ? "501 Not Implemented" :
            peticionHTTP->fichero.existe ? "200 OK" : "404 Not Found" ) );

    /* Fecha actual */
    time( &fecha );
    posicion += sprintf( posicion, "Date: %s", ctime( &fecha ) );
    // Se decrementa el puntero en 1 dado que ctime devuelve un '\n'
    posicion--;
    // Y se añade la nueva línea
    posicion += sprintf( posicion, "\r\n" );

    /* Nombre del servidor */
    posicion += sprintf( posicion, "Server: Windows XP Server Edition "
                        "2003\r\n" );

    // Si el fichero solicitado existe, se añade su correspondiente información
    if( peticionHTTP->fichero.existe )
    {
        /* Fecha de última modificación del fichero */
        posicion += sprintf( posicion, "Content Last-Modified: %s",
                ctime( &( peticionHTTP->fichero.estado.st_mtime ) ) );
        // Se decrementa el puntero en 1 dado que ctime devuelve un '\n'
        posicion--;
        // Y se añade la nueva línea
        posicion += sprintf( posicion, "\r\n" );

        /* Unidad de tamaño */
        posicion += sprintf( posicion, "Accept-Ranges: bytes\r\n" );

        /* Tamaño del fichero */
        posicion += sprintf( posicion, "Content-Length: %d\r\n",
                peticionHTTP->fichero.numeroBytes );

        /* Tipo del fichero */
        posicion += sprintf( posicion, "Content-Type: %s\r\n",
                peticionHTTP->tipoContenido );
    }

    /* Se mantiene o no abierta la conexión*/
    posicion += sprintf( posicion, "Connection: %s\r\n",
            ( peticionHTTP->keepAlive ? "keep-alive" : "close" ) );

    /* Línea en blanco */
    posicion += sprintf( posicion, "\r\n" );

    /* Contenido del fichero, si existe */
    if( peticionHTTP->fichero.existe )
    {
        memcpy( posicion, peticionHTTP->fichero.contenido,
            peticionHTTP->fichero.numeroBytes );
        /*posicion += sprintf( posicion, "%s",
                peticionHTTP->fichero.contenido );*/
    }

    // Se guarda la dirección del buffer en la estructura de la petición
    peticionHTTP->respuesta = buffer;

    peticionHTTP->tamanoRespuesta = ( posicion - buffer ) +
        peticionHTTP->fichero.numeroBytes;
}


void *funcionHilo( void *arg )
{
    // Mensaje enviado/recibido
    char mensaje[ LEN ];

    // Estructura en la que guardar los datos de la petición HTTP
    struct peticion peticionHTTP;

    // Número de bytes enviados al cliente
    ssize_t bytesTransmitidos = 0;

    // Puntero a la estructura recibido como argumento
    struct infoHilo *info = ( struct infoHilo * )arg;


    // Se indica la creación del hilo
    printf( "[!] Soy el hilo nº %lu\n\n", info->numeroHilo );

    // Si se ha producido con éxito la conexión, se imprimen los
    // detalles del cliente conectado
    printf
    (
        "[!] Conexión establecida con el cliente %d:\n"
        "\t--> Dirección IP del cliente: %s\n"
        "\t--> Puerto: %u\n\n",
        info->socketConexion,
        inet_ntoa( info->direccion.sin_addr ),
        info->direccion.sin_port
    );

    // Se indica que el mantener la conexión abierta hasta que el cliente
    // indique lo contrario
    peticionHTTP.keepAlive = 1;

    // Mientras el cliente no indique la finalización de la conexión
    while( ( bytesTransmitidos = recv( info->socketConexion, mensaje, LEN,
        0 ) ) > 0 && peticionHTTP.keepAlive )
    {
        printf( "└───> He recibido una petición:\n\n" );

        // Se imprime la cantidad de bytes recibidos del cliente
        printf
        (
            "[!] Cantidad de bytes recibidos del cliente %d: %d\n"
            "[!] Mensaje:\n%s\n\n",
            info->socketConexion,
            ( int )bytesTransmitidos,
            mensaje
        );

        // Se indica inicialmente que sí se ha recibido una petición correcta
        peticionHTTP.peticionCorrecta = 1;
        // Y que el fichero solicitado existe
        peticionHTTP.fichero.existe = 1;

        // Se descompone la primera línea del mensaje
        descomponerMensaje( mensaje, &peticionHTTP );

        // Se determina el tipo de contenido
        determinarContenido( &peticionHTTP );

        // Y se cargan los datos del fichero pedido
        cargarFichero( &peticionHTTP );

        /*// Se imprime el mensaje descompuesto
        printf
        (
            "[!] Comando de la petición: %s\n"
            "[!] Ruta de la petición: %s\n"
            "[!] Protocolo de la petición: %s\n"
            "[!] Keep-Alive: %s\n"
            "[!] Tipo de contenido: %s\n"
            "[!] Existe el fichero: %s\n\n\n",
            peticionHTTP.comando,
            peticionHTTP.ruta,
            peticionHTTP.protocolo,
            ( peticionHTTP.keepAlive == 1 ) ? "sí" : "no",
            peticionHTTP.tipoContenido,
            ( peticionHTTP.fichero.existe == 1 ) ? "sí" : "no"
        );

        // Si existe el fichero, se imprime información adicional
        if( peticionHTTP.fichero.existe )
        {
            printf
            (
                "[!] Nº de bytes del fichero: %d\n"
                "[!] Última modificación del fichero: %s"
                "[!] Contenido del fichero:\n%s\n\n\n",
                peticionHTTP.fichero.numeroBytes,
                ctime( &peticionHTTP.fichero.estado.st_mtime),
                peticionHTTP.fichero.contenido
            );
        }

        printf( "Voy a componer una respuesta\n" );*/
        // Se compone una respuesta
        componerRespuesta( &peticionHTTP );

        // Es muy probable que se muestren caracteres "extraños" dado que
        // también se imprime el contenido del archivo, y no tiene por qué ser
        // texto
        printf( "└───> Respuesta:\n\n%s\n\n", peticionHTTP.respuesta );

        // Se devuelve la respuesta creada al cliente
        //peticionHTTP.tamanoRespuesta = strlen( peticionHTTP.respuesta ) + 1;
        if( ( bytesTransmitidos = send( info->socketConexion,
            peticionHTTP.respuesta, peticionHTTP.tamanoRespuesta, 0 ) ) < 0 )
        {
            perror( "No se ha podido mandar una respuesta al cliente" );
            exit( EXIT_FAILURE );
        }

        // Se imprime la cantidad de bytes enviados al cliente
        printf( "[!] Cantidad de bytes enviados al cliente: %d\n\n\n",
            ( int )bytesTransmitidos );

        // Y se libera la información guardada
        free( peticionHTTP.comando );
        free( peticionHTTP.ruta );
        free( peticionHTTP.protocolo );
        if( peticionHTTP.fichero.existe )
            free( peticionHTTP.fichero.contenido );
        free( peticionHTTP.respuesta );
    }

    // Se cierra el socket de conexión con el cliente
    if( close( info->socketConexion ) < 0 )
    {
        perror( "No se ha podido cerrar el socket de conexión" );
        exit( EXIT_FAILURE );
    }

    // Se bloquea el mutex del número de hilos activos para evitar carreras
    // críticas
    pthread_mutex_lock( info->mutex );
    // Se reduce en uno el número de hilos activos
    *( info->hilosActivos ) -= 1;
    // Se desbloquea el mutex
    pthread_mutex_unlock( info->mutex );

    // Se libera la memoria reservada al crear el hilo
    free( info );
    
    // Y se finaliza la ejecución
    pthread_exit( EXIT_SUCCESS );
}


int servidor( char* puertoArgumento )
{
    // Identificadores de los sockets
    int socketEscucha = 0;
    int socketConexion = 0;

    // Puerto de escucha
    int puertoEscucha = 0;

    // Dirección para un socket
    struct sockaddr_in direccion;
    // Tamaño de la dirección
    socklen_t tamano = 0;

    // Puntero a una estructura para un hilo
    struct infoHilo *infoHilo = NULL;

    // TID de un hilo
    pthread_t tid = 0;

    // Número de hilos activos
    int hilosActivos = 0;

    // Mutex con el que controlar el acceso al número de hilos activos
    pthread_mutex_t mutex;

    // Atributos empleados en la creación de los hilos
    pthread_attr_t atributos;


    // Se inicializa el mutex
    if( pthread_mutex_init( &mutex, NULL ) != 0 )
    {
        perror( "No se ha podido inicializar el mutex" );
        exit( EXIT_FAILURE );
    }

    // Se inicializa la estructura de atributos a los valores predeterminados
    pthread_attr_init( &atributos );
    // Y se indica que se quieren crear hilos apartados
    pthread_attr_setdetachstate( &atributos, PTHREAD_CREATE_DETACHED );

    // Se crea el socket: IPv4, orientado a conexión, protocolo automático
    if( ( socketEscucha = socket( PF_INET, SOCK_STREAM, 0 ) ) < 0 )
    {
        perror( "No se ha podido crear el socket" );
        exit( EXIT_FAILURE );
    }

    // Dirección a asignar al socket
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = htonl( INADDR_ANY );
    sscanf( puertoArgumento, "%d", &puertoEscucha );
    direccion.sin_port = htons( puertoEscucha );
    tamano = sizeof( struct sockaddr_in );

    // Se asiga al socket creado la dirección
    if( bind( socketEscucha, ( struct sockaddr * )&direccion, tamano ) < 0 )
    {
        perror( "No se pudo asignar una dirección al socket" );
        exit( EXIT_FAILURE );
    }

    // Se marca el socket como uno en el que escuchar por conexiones
    if( listen( socketEscucha, MAX_SOLICITUDES_COLA ) < 0 )
    {
        perror( "No se ha podido comenzar a escuchar las conexiones" );
        exit( EXIT_FAILURE );
    }

    while( 1 )
    {
        // Se acepta una solicitud de conexión al recibirla, creando un nuevo
        // socket en el que recibir los datos del cliente
        if( ( socketConexion = accept( socketEscucha,
            ( struct sockaddr * )&direccion, &tamano ) ) < 0 )
        {
            perror( "No se ha podido aceptar una solicitud de conexión" );
            exit( EXIT_FAILURE );
        }

        // Antes de nada, se bloquea el mutex del número de hilos activos para
        // que los hilos no lo puedan modificar
        pthread_mutex_lock( &mutex );

        // Se reserva memoria para un hilo
        if( ( infoHilo = malloc( sizeof( struct infoHilo ) ) ) == NULL )
        {
            perror( "Reserva de memoria fallida\n" );
            exit( EXIT_FAILURE );
        }

        // Se copian los contenidos al argumento que se pasará al hilo y se
        // inicializan los demás campos al valor apropiado
        infoHilo->socketConexion = socketConexion;
        infoHilo->numeroHilo = hilosActivos;
        memcpy( &( infoHilo->direccion ), &direccion, tamano );
        infoHilo->tamanoDireccion = tamano;
        infoHilo->hilosActivos = &hilosActivos;
        infoHilo->mutex = &mutex;

        // Se crea el hilo
        if( pthread_create( &tid, &atributos, funcionHilo,
            ( void * )infoHilo ) != 0 )
        {
            perror( "No se ha podido crear un hilo" );
            exit( EXIT_FAILURE );
        }

        // Se incrementa en uno el número de hilos activos
        hilosActivos++;

        // Y de desbloquea el mutex del número de hilos activos
        pthread_mutex_unlock( &mutex );
    }

    // Finalmente, se cierran el socket de escucha
    if( close( socketEscucha ) < 0 )
    {
        perror( "No se ha podido cerrar el socket de escucha" );
        exit( EXIT_FAILURE );
    }

    exit( EXIT_SUCCESS );
}
