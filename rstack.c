// Tomasz Podlaszewski

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include <inttypes.h>
#include <errno.h>
#include "rstack.h"

#define BUFFER_SIZE 8192
#define FORMAT_STR "%8191s"

// Zmienna globalna do przechodzenia po stosach
// unikajac zacyklenia. Przed kazdym uzyciem robie
// current_visited_id++ co generuje "nowa" flage.
static uint64_t current_visited_id = 1;

typedef struct node
{
    bool is_int;
    union
    {
        uint64_t val;
        rstack_t *rstack;
    } data;
    struct node *next;
} node_t;

typedef struct rstack
{
    node_t *top;
    size_t ref_count;
    // Uzywane w funkcji rstack_delete do znajdywania stosow
    // do ktorych nie mozna dostac sie z nadal uzywanych stosow.
    size_t av_from;
    // Uzywane w funkcji rstack_delete do oznaczania stosow do usuniecia.
    bool dead;
    // Uzywane do wykrywania cykli, jesli rowne current_visited_id,
    // to odwiedzone w aktualnym przeszukiwaniu.
    uint64_t visited_id;
} rstack_t;

rstack_t *rstack_new()
{
    rstack_t *new = malloc(sizeof(rstack_t));
    if (new == nullptr)
    {
        errno = ENOMEM;
    }
    else
    {
        new->ref_count = 1;
        new->av_from = 0;
        new->visited_id = 0;
        new->top = nullptr;
        new->dead = false;
    }

    return new;
}

// Do usuwania stosow uzywam algorytmu Trial Deletion.
// Polega on na poczatkowym oznaczeniu wszystkich stosow
// jako do usuniecia, pozniej sprawdzamy, ktore z nich
// leza na jakims "zywym" stosie - tych nie nalezy usuwac.

// Funkcja resetujaca flagi przed Trial Deletion
void rstack_delete_reset_flags(rstack_t *rs)
{
    if (rs == nullptr || rs->visited_id == current_visited_id)
        return;

    rs->visited_id = current_visited_id;
    rs->av_from = 0;
    rs->dead = true;

    node_t *curr = rs->top;
    while (curr != nullptr)
    {
        if (!curr->is_int)
            rstack_delete_reset_flags(curr->data.rstack);
        curr = curr->next;
    }
}

// Funkcja pomocnicza do rstack_delete,
// stosowana do zaznaczania na ilu innych stosach znajduja sie stosy.
void rstack_delete_traverse(rstack_t *rs)
{
    if (rs == nullptr || rs->visited_id == current_visited_id)
        return;

    rs->visited_id = current_visited_id;
    node_t *curr = rs->top;

    while (curr != nullptr)
    {
        if (!curr->is_int)
        {
            curr->data.rstack->av_from++;
            rstack_delete_traverse(curr->data.rstack);
        }
        curr = curr->next;
    }
}

// Funkcja pomocnicza, odznaczajaca stosy bedace na zywym stosie jako zywe.
// Musimy to robic aby uniknac use after free.
void rstack_delete_rescue(rstack_t *rs)
{
    // Wychodzimy gdy rs->dead == false, bo
    // wtedy juz obsluzylismy ten stos.
    if (rs == nullptr || !rs->dead)
        return;

    rs->dead = false;

    node_t *curr = rs->top;
    while (curr != nullptr)
    {
        if (!curr->is_int)
            rstack_delete_rescue(curr->data.rstack);
        curr = curr->next;
    }
}

// Trzecia faza Trial Deletion - oznaczanie stosow do usuniecia
void rstack_delete_assess(rstack_t *rs)
{
    if (rs == nullptr || rs->visited_id == current_visited_id)
        return;

    rs->visited_id = current_visited_id;
    node_t *curr = rs->top;

    // Jesli ref_count > av_from
    // stos lezy na jakims uzywanym stosie,
    // czyli jest zywy.
    if (rs->ref_count > rs->av_from)
    {
        rstack_delete_rescue(rs);
    }

    while (curr != nullptr)
    {
        if (!curr->is_int)
        {
            rstack_delete_assess(curr->data.rstack);
        }
        curr = curr->next;
    }
}

// ostatnia faza Trial Deletion - usuwanie stosow.
void rstack_delete_execute(rstack_t *rs)
{
    if (rs == nullptr || !rs->dead)
        return;

    node_t *curr = rs->top;
    // Odpinamy listę, chroni przed zablokowaniem w cyklu
    rs->top = nullptr;

    while (curr != nullptr)
    {
        node_t *temp = curr->next;

        if (!curr->is_int)
        {
            rstack_t *child = curr->data.rstack;

            if (child->dead)
            {
                if (child->top != nullptr)
                {
                    rstack_delete_execute(child);
                }

                child->ref_count--;
                if (child->ref_count == 0)
                {
                    free(child);
                }
            }
            else
            {
                rstack_delete(child);
            }
        }
        free(curr);
        curr = temp;
    }
}

// Glowna funkcja.
void rstack_delete(rstack_t *rs)
{
    if (rs != nullptr)
    {
        rs->ref_count--;
        if (rs->ref_count == 0)
        {
            while (rs->top != nullptr)
            {
                rstack_pop(rs);
            }
            free(rs);
        }
        else
        {
            current_visited_id++;
            rstack_delete_reset_flags(rs);

            current_visited_id++;
            rstack_delete_traverse(rs);

            current_visited_id++;
            rstack_delete_assess(rs);

            if (rs->dead)
            {
                // Zabezpieczenie przed przedwczesnym
                // free() w przypadku cyklu.
                rs->ref_count++;
                rstack_delete_execute(rs);
                // Zdjęcie zabezpieczenia.
                rs->ref_count--;

                // Bezpieczne zwolnienie pamięci po wyczyszczeniu krawędzi.
                if (rs->ref_count == 0)
                {
                    free(rs);
                }
            }
        }
    }
}

int rstack_push_value(rstack_t *rs, uint64_t value)
{
    if (rs == nullptr)
    {
        errno = EINVAL;
        return -1;
    }

    node_t *new = malloc(sizeof(node_t));
    if (new == nullptr)
    {
        errno = ENOMEM;
        return -1;
    }

    new->is_int = true;
    new->data.val = value;
    new->next = rs->top;
    rs->top = new;
    return 0;
}

int rstack_push_rstack(rstack_t *rs1, rstack_t *rs2)
{
    if (rs1 == nullptr || rs2 == nullptr)
    {
        errno = EINVAL;
        return -1;
    }

    node_t *new = malloc(sizeof(node_t));
    if (new == nullptr)
    {
        errno = ENOMEM;
        return -1;
    }

    new->is_int = false;
    new->data.rstack = rs2;
    new->next = rs1->top;
    rs2->ref_count++;
    rs1->top = new;
    return 0;
}

void rstack_pop(rstack_t *rs)
{
    if (rs != nullptr && rs->top != nullptr)
    {
        node_t *popped_node = rs->top;
        rs->top = popped_node->next; // Najpierw fizycznie odpinamy od stosu

        if (popped_node->is_int)
        {
            free(popped_node);
        }
        else
        {
            rstack_delete(popped_node->data.rstack);
            free(popped_node);
        }
    }
}

bool rstack_empty_helper(rstack_t *rs)
{
    if (rs == nullptr)
        return true;

    node_t *curr = rs->top;

    while (curr != nullptr)
    {
        if (curr->is_int)
        {
            return false;
        }
        else if (curr->data.rstack->visited_id != current_visited_id)
        {
            curr->data.rstack->visited_id = current_visited_id;
            if (!rstack_empty_helper(curr->data.rstack))
                return false;
        }
        curr = curr->next;
    }
    return true;
}

bool rstack_empty(rstack_t *rs)
{
    if (rs == nullptr)
        return true;

    current_visited_id++;
    rs->visited_id = current_visited_id;

    return rstack_empty_helper(rs);
}

result_t rstack_front_helper(rstack_t *rs)
{
    result_t out;
    out.flag = false;
    out.value = 0;

    if (rs == nullptr)
        return out;

    rs->visited_id = current_visited_id;

    node_t *curr = rs->top;

    while (curr != nullptr)
    {
        if (curr->is_int)
        {
            out.flag = true;
            out.value = curr->data.val;
            return out;
        }
        else if (curr->data.rstack->visited_id != current_visited_id)
        {
            result_t temp = rstack_front_helper(curr->data.rstack);
            if (temp.flag)
                return temp;
        }
        curr = curr->next;
    }
    return out;
}

result_t rstack_front(rstack_t *rs)
{
    current_visited_id++;
    return rstack_front_helper(rs);
}

rstack_t *rstack_read(char const *path)
{
    if (path == nullptr)
    {
        errno = EINVAL;
        return nullptr;
    }

    FILE *file = fopen(path, "r");
    if (file == nullptr)
    {
        // errno ustawione przez fopen
        return nullptr;
    }

    rstack_t *rs = rstack_new();
    if (rs == nullptr)
    {
        // errno ustawione na ENOMEM przez rstack_new
        int errno_temp = errno;
        fclose(file);
        errno = errno_temp;
        return nullptr;
    }

    char buffer[BUFFER_SIZE];

    // FORMAT_STR, czyli %8191s wczyta ciąg nie-białych znaków o maksymalnej
    // długości 8191. Moj program akceptuje liczby z zerami wiodacymi o ile ich
    // dlugosc nie przekroczy 8191. Cytujac forum:
    // "Nie specyfikujemy sposobu obsługi zer wiodących w danych wejściowych."
    while (fscanf(file, FORMAT_STR, buffer) == 1)
    {
        // Upewnienie sie, ze string sklada sie tylko z cyfr.
        for (int i = 0; buffer[i] != '\0'; i++)
        {
            if (!isdigit(buffer[i]))
            {
                rstack_delete(rs);
                fclose(file);
                errno = EINVAL;
                return nullptr;
            }
        }

        char *endptr;
        int errno_temp = errno;
        // Resetujemy przed stroull, zeby wychwycic czy zwroci blad.
        errno = 0;

        uint64_t value = (uint64_t)strtoull(buffer, &endptr, 10);

        // Jesli endptr nie ruszyl z miejsca lub nie dotarlismy do konca
        // lub wystapil ERANGE (overflow uint64_t).
        if (endptr == buffer || *endptr != '\0' || errno != 0)
        {
            // Jesli liczba w buforze byla zbyt duza stroull ustawi errno na ERANGE,
            // co jest bardziej precyzyjne niz EINVAL. Jesli powod bledu byl inny, to
            // ustawiamy einval.
            int final_errno = (errno == ERANGE) ? ERANGE : EINVAL;
            rstack_delete(rs);
            fclose(file);
            errno = final_errno;
            return nullptr;
        }

        if (rstack_push_value(rs, value) != 0)
        {
            // Zapisujemy poprane errno ustawione przez push_value
            // przed fclose.
            int errno_t = errno;
            rstack_delete(rs);
            fclose(file);
            errno = errno_t;
            return nullptr;
        }

        errno = errno_temp;
    }

    // Upewniamy sie, ze powodem przerwania
    // petli bylo dojscie do konca pliku.
    if (!feof(file))
    {
        rstack_delete(rs);
        fclose(file);
        // Jeśli fscanf zgłosił error a to nie
        // jest EOF, oznacza to problem z plikiem.
        errno = EIO;
        return nullptr;
    }

    fclose(file);
    return rs;
}

// Funkcja pomocnicza do rstack_write, zwraca 0 gdy bez bledow,
// -1 jesli wystapil jakis blad i 1 jesli znaleziono cykl.
int rstack_write_helper(FILE *file, node_t *n)
{
    if (n == nullptr)
        return 0;

    // Zapis z dołu do góry
    int result = rstack_write_helper(file, n->next);

    if (result == -1 || result == 1)
        return result;

    if (n->is_int)
    {
        if (fprintf(file, "%" PRIu64 "\n", n->data.val) < 0)
            return -1; // errno jest ustawiane przez fprintf.
    }
    else
    {
        if (n->data.rstack->visited_id == current_visited_id)
        {
            return 1; // znaleziony cykl
        }
        else
        {
            n->data.rstack->visited_id = current_visited_id;
            result = rstack_write_helper(file, n->data.rstack->top);

            // Zdejmujemy flage "odwiedzonego w tej sciezce".
            // Dzieki temu inna gałaz grafu
            // będzie mogła do niego wejsc i go wydrukowac.
            if (result == 0)
                n->data.rstack->visited_id--;

            return result;
        }
    }
    return 0;
}

int rstack_write(char const *path, rstack_t *rs)
{
    if (path == nullptr || rs == nullptr)
    {
        errno = EINVAL;
        return -1;
    }

    FILE *file = fopen(path, "w");
    if (file == nullptr)
    {
        // fopen samo ustawia errno przy bledzie.
        return -1;
    }

    current_visited_id++;
    rs->visited_id = current_visited_id;

    int result = rstack_write_helper(file, rs->top);
    int errno_temp = errno;

    int fclose_result = fclose(file);

    if (result == -1)
    {
        errno = errno_temp; // przywracamy wlasciwy blad sprzed fclose.
        return -1;
    }

    if (fclose_result != 0)
    {
        // errno jest juz automatycznie ustawione przez fclose
        return -1;
    }

    return 0;
}
