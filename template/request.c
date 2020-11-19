#include "io_helper.h"
#include "request.h"

#define MAXBUF (8192)

/* Code to queue of requests */

// Structure stores information about a request
struct request_record
{
    // File descriptor of connection
    int fd;
    // Relative path of the file
    char *filepath;
    // Size of the file
    int filesize;
};

struct request_record new_request_record(int fd, char *path, int fsize)
{
    struct request_record new_record;
    new_record.fd = fd;
    new_record.filepath = strdup(path);
    new_record.filesize = fsize;
    return new_record;
}

// Structure for a node in linked list
struct list_node
{
    // Stores a request body
    struct request_record req;
    // Pointer to next node
    struct list_node *next;
};

struct list_node new_node(struct request_record r)
{
    struct list_node ln;
    ln.req = r;
    ln.next = NULL;
}

enum list_type
{
    FIFO,
    SFF
};

struct linked_list
{
    // Stores the type of linked list
    // Whether sorted or naive
    enum list_type lltype;
    // Pointet to the head of the list
    struct list_node *head;
    // Size of the linked list
    int size;
};

//
//	TODO: add code to create and manage the buffer
//

// Creates a new blank linked list
struct linked_list new_queue(enum list_type ltype)
{
    struct linked_list rlist;
    rlist.head = NULL;
    rlist.lltype = ltype;
    rlist.size = 0;
    return rlist;
}

int enqueue(struct linked_list list, struct request_record r)
{
    if (list.head == NULL)
    {
        list.head = malloc(sizeof(struct list_node));
        list.head->req = r;
        list.head->next = NULL;
    }
    else
    {   
        // If the type of list is SFF
        if (list.lltype == SFF)
        {   
            // Iterate to a suitable position
            struct list_node *iter = list.head;
            // Move forward until we reach a filesize greater than equal to specified
            // OR
            // Move forward until the end of the list
            while (iter->next != NULL && iter->req.filesize < r.filesize)
            {
                iter = iter->next;
            }
            // If we have reached the end of the list
            if (iter->next == NULL)
            {
                // Create a new node and attach it at the end
                iter->next = malloc(sizeof(struct list_node));
                iter->next->req = r;
                iter->next->next = NULL;
            }
            // Otherwise
            else
            {   
                //  Insert a new node in the list
                struct list_node * old_next = iter->next;
                iter->next = malloc(sizeof(struct list_node));
                iter->next->req = r;
                iter->next->next = old_next;
            }
        }
        // If the list is naive FIFO
        else
        {
            // Iterate to the end of the list
            struct list_node * iter = list.head;
            while(iter->next != NULL) 
            {
                iter = iter->next;
            }
            // Create a new node at the end
            iter->next = malloc(sizeof(struct list_node));
            iter->next->req = r;
            iter->next->next = NULL;
        }
    }
    // Increment the list size
    list.size++;
    // Return the incremented size
    return list.size;
}

//
// Sends out HTTP response in case of errors
//
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXBUF], body[MAXBUF];

    // Create the body of error message first (have to know its length for header)
    sprintf(body, ""
                  "<!doctype html>\r\n"
                  "<head>\r\n"
                  "  <title>OSTEP WebServer Error</title>\r\n"
                  "</head>\r\n"
                  "<body>\r\n"
                  "  <h2>%s: %s</h2>\r\n"
                  "  <p>%s: %s</p>\r\n"
                  "</body>\r\n"
                  "</html>\r\n",
            errnum, shortmsg, longmsg, cause);

    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));

    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));

    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));

    // Write out the body last
    write_or_die(fd, body, strlen(body));

    // close the socket connection
    close_or_die(fd);
}

//
// Reads and discards everything up to an empty text line
//
void request_read_headers(int fd)
{
    char buf[MAXBUF];

    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n"))
    {
        readline_or_die(fd, buf, MAXBUF);
    }
    return;
}

//
// Return 1 if static, 0 if dynamic content (executable file)
// Calculates filename (and cgiargs, for dynamic) from uri
//
int request_parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;

    if (!strstr(uri, "cgi"))
    {
        // static
        strcpy(cgiargs, "");
        sprintf(filename, ".%s", uri);
        if (uri[strlen(uri) - 1] == '/')
        {
            strcat(filename, "index.html");
        }
        return 1;
    }
    else
    {
        // dynamic
        ptr = index(uri, '?');
        if (ptr)
        {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        }
        else
        {
            strcpy(cgiargs, "");
        }
        sprintf(filename, ".%s", uri);
        return 0;
    }
}

//
// Fills in the filetype given the filename
//
void request_get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else
        strcpy(filetype, "text/plain");
}

//
// Handles requests for static content
//
void request_serve_static(int fd, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];

    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);

    // Rather than call read() to read the file into memory,
    // which would require that we allocate a buffer, we memory-map the file
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);

    // put together response
    sprintf(buf, ""
                 "HTTP/1.0 200 OK\r\n"
                 "Server: OSTEP WebServer\r\n"
                 "Content-Length: %d\r\n"
                 "Content-Type: %s\r\n\r\n",
            filesize, filetype);

    write_or_die(fd, buf, strlen(buf));

    //  Writes out to the client socket the memory-mapped file
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}

//
// Fetches the requests from the buffer and handles them (thread locic)
//
void *thread_request_serve_static(void *arg)
{
    // TODO: write code to actualy respond to HTTP requests
}

//
// Initial handling of the request
//
void request_handle(int fd)
{
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];

    // get the request type, file path and HTTP version
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);

    // verify if the request type is GET is not
    if (strcasecmp(method, "GET"))
    {
        request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
        return;
    }
    request_read_headers(fd);

    // check requested content type (static/dynamic)
    is_static = request_parse_uri(uri, filename, cgiargs);

    // get some data regarding the requested file, also check if requested file is present on server
    if (stat(filename, &sbuf) < 0)
    {
        request_error(fd, filename, "404", "Not found", "server could not find this file");
        return;
    }

    // verify if requested content is static
    if (is_static)
    {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
        {
            request_error(fd, filename, "403", "Forbidden", "server could not read this file");
            return;
        }

        // TODO: write code to add HTTP requests in the buffer based on the scheduling policy
    }
    else
    {
        request_error(fd, filename, "501", "Not Implemented", "server does not serve dynamic content request");
    }
}
