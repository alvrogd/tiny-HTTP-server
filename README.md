# tiny-HTTP-server

## ¿De qué se trata?

Aquí se encuentra un sencillo __servidor web, programado en C mediante sockets TCP__, que es capaz de proporcionar contenidos a un navegador web.

De forma resumida, el funcionamiento del servidor consiste en:

  1. Esperar por una solicitud de conexión.
  2. Procesar una petición cuando llegue, extrayendo la URL del recurso solicitado.
  3. Elaborar un mensaje de respuesta, incluyendo tanto la cabecera como el cuerpo.
  4. Si se emplean conexiones no persistentes, debe cerrarse el socket de conexión. En caso contario, se espera la siguiente petición del cliente.
  
El contenido de la cabecera de una respuesta consiste en las siguientes líneas:

 * Obligatoriamente:
    * Línea de estado (por ejemplo, '200 OK').
    * Descripción.
    
 * Opcionalmente:
    * Fecha local.
    * Nombre del servidor.
    * Fecha de última modificación del recurso.
    * Tamaño en bytes del contenido solicitado.
    * Tipo del contenido solicitado.
    * Si la conexión es persistente o no.
    
    
## ¿Cómo se usa?

El servidor puede ser compilado con el siguiente comando:

```
gcc servidor.c -pthread -o servidor
```

Y, para ejecutarlo, debe indicarse al ejecutable un puerto en el cual el servidor escuchará las peticiones dirigidas a él:

```
./servidor 8000
```

Si no se indica un puerto superior a 1024, es necesario contar con permisos de superusuario.


## Trabajo futuro

En la actual implementación del servidor, a pesar de ser funcional, no se puede considerar como más que un prototipo de servidor web HTTP; por ejemplo, el único formato de audio reconocido es el _aac_.


Queda como un trabajo futuro el mejorar la actual implementación del servidor, tanto en cuanto a calidad de la programación, como en cuanto a funcionalidad.
