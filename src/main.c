#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <semaphore.h>

#define SHM_NAME "/my_shm"
#define SEM_WRITE "/sem_write"
#define SEM_READ "/sem_read"
#define SHM_SIZE 4096

typedef struct {
    char buffer[1024];
    int size;
    int finished;
} shared_data;

void write_string(int fd, const char *str)
{
    write(fd, str, strlen(str));
}

int main()
{
    pid_t pid;
    char filename[256];
    char buffer[1024];
    ssize_t bytes_read;
    int shm_fd;
    shared_data *shm_ptr;
    sem_t *sem_write, *sem_read;

    write_string(STDOUT_FILENO, "Enter filename: ");
    bytes_read = read(STDIN_FILENO, filename, sizeof(filename) - 1);
    if (bytes_read <= 0)
    {
        write_string(STDERR_FILENO, "read error\n");
        exit(EXIT_FAILURE);
    }
    filename[bytes_read - 1] = '\0';

    // Создаем разделяемую память
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        write_string(STDERR_FILENO, "shm_open error\n");
        exit(EXIT_FAILURE);
    }

    if (ftruncate(shm_fd, SHM_SIZE) == -1)
    {
        write_string(STDERR_FILENO, "ftruncate error\n");
        exit(EXIT_FAILURE);
    }

    shm_ptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED)
    {
        write_string(STDERR_FILENO, "mmap error\n");
        exit(EXIT_FAILURE);
    }

    // Создаем семафоры для синхронизации
    sem_unlink(SEM_WRITE);
    sem_unlink(SEM_READ);
    sem_write = sem_open(SEM_WRITE, O_CREAT, 0666, 0);
    sem_read = sem_open(SEM_READ, O_CREAT, 0666, 0);
    
    if (sem_write == SEM_FAILED || sem_read == SEM_FAILED)
    {
        write_string(STDERR_FILENO, "sem_open error\n");
        exit(EXIT_FAILURE);
    }

    shm_ptr->finished = 0;

    pid = fork();

    if (pid == -1)
    {
        write_string(STDERR_FILENO, "fork error\n");
        exit(EXIT_FAILURE);
    }

    if (pid == 0)
    {
        // Дочерний процесс
        execl("./child", "child", filename, NULL);
        write_string(STDERR_FILENO, "execl error\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        // Родительский процесс
        write_string(STDOUT_FILENO, "Enter numbers separated by spaces: ");
        while ((bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0)
        {
            // Записываем данные в разделяемую память
            memcpy(shm_ptr->buffer, buffer, bytes_read);
            shm_ptr->size = bytes_read;
            
            // Сигнализируем дочернему процессу
            sem_post(sem_write);
            
            // Ждем обработки
            sem_wait(sem_read);
            
            // Проверяем, не завершился ли дочерний процесс
            if (shm_ptr->finished)
            {
                break;
            }

            int status;
            pid_t res = waitpid(pid, &status, WNOHANG);
            if (res != 0)
            {
                break;
            }

            write_string(STDOUT_FILENO, "Enter next numbers: ");
        }
        
        // Сигнализируем о завершении
        shm_ptr->finished = 1;
        sem_post(sem_write);
        
        waitpid(pid, NULL, 0);
        
        // Очистка ресурсов
        munmap(shm_ptr, SHM_SIZE);
        close(shm_fd);
        shm_unlink(SHM_NAME);
        sem_close(sem_write);
        sem_close(sem_read);
        sem_unlink(SEM_WRITE);
        sem_unlink(SEM_READ);
    }

    return 0;
}