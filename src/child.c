#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
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

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        write_string(STDERR_FILENO, "Usage: child <filename>\n");
        exit(EXIT_FAILURE);
    }

    int file = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file == -1)
    {
        write_string(STDERR_FILENO, "open error\n");
        exit(EXIT_FAILURE);
    }

    // Открываем разделяемую память
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        write_string(STDERR_FILENO, "shm_open error\n");
        exit(EXIT_FAILURE);
    }

    shared_data *shm_ptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED)
    {
        write_string(STDERR_FILENO, "mmap error\n");
        exit(EXIT_FAILURE);
    }

    // Открываем семафоры
    sem_t *sem_write = sem_open(SEM_WRITE, 0);
    sem_t *sem_read = sem_open(SEM_READ, 0);
    
    if (sem_write == SEM_FAILED || sem_read == SEM_FAILED)
    {
        write_string(STDERR_FILENO, "sem_open error\n");
        exit(EXIT_FAILURE);
    }

    char line[1024];
    int line_pos = 0;

    while (1)
    {
        // Ждем данные от родителя
        sem_wait(sem_write);
        
        if (shm_ptr->finished)
        {
            sem_post(sem_read);
            break;
        }

        // Обрабатываем данные из разделяемой памяти
        for (int i = 0; i < shm_ptr->size; i++)
        {
            if (shm_ptr->buffer[i] == '\n')
            {
                line[line_pos] = '\0';

                float numbers[100];
                int count = 0;
                char *token = strtok(line, " ");
                while (token && count < 100)
                {
                    numbers[count++] = atof(token);
                    token = strtok(NULL, " ");
                }

                if (count < 2)
                {
                    line_pos = 0;
                    write_string(1, "Error: Need at least 2 numbers\n");
                    continue;
                }
                else
                {
                    float dividend = numbers[0];
                    for (int j = 1; j < count; j++)
                    {
                        if (numbers[j] == 0.0f)
                        {
                            write_string(file, "Division by zero detected. Exiting.\n");
                            write_string(1, "Division by zero detected. Exiting.\n");
                            close(file);
                            
                            // Устанавливаем флаг завершения и освобождаем семафор
                            shm_ptr->finished = 1;
                            sem_post(sem_read);
                            
                            munmap(shm_ptr, SHM_SIZE);
                            close(shm_fd);
                            sem_close(sem_write);
                            sem_close(sem_read);
                            exit(EXIT_FAILURE);
                        }
                        char result[64];
                        float res = dividend / numbers[j];
                        int len = snprintf(result, sizeof(result), "%.2f / %.2f = %.2f\n", dividend, numbers[j], res);
                        write(file, result, len);
                    }
                }
                line_pos = 0;
            }
            else
            {
                if (line_pos < sizeof(line) - 1)
                {
                    line[line_pos++] = shm_ptr->buffer[i];
                }
            }
        }
        
        // Сигнализируем родителю о завершении обработки
        sem_post(sem_read);
    }

    close(file);
    munmap(shm_ptr, SHM_SIZE);
    close(shm_fd);
    sem_close(sem_write);
    sem_close(sem_read);
    return 0;
}