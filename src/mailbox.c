#include <mailbox.h>
#include <stdlib.h>
#include <string.h>

void mailbox_init(Mailbox* mailbox) {
    mailbox->head = NULL;
    mailbox->tail = NULL;
    mailbox->len = 0;
    pthread_mutex_init(&mailbox->lock, NULL);
}

void mailbox_destroy(Mailbox* mailbox) {
    pthread_mutex_lock(&mailbox->lock);
    Message* msg = mailbox->head;
    while (msg) {
        Message* next = msg->next;
        free(msg->payload);
        free(msg);
        msg = next;
    }
    pthread_mutex_unlock(&mailbox->lock);
}

void mailbox_send_message(Mailbox* dest, void* data, size_t size) {
    Message* msg = malloc(sizeof(Message));
    if (!msg) return; // gérer l’erreur

    msg->payload = malloc(size);
    if (!msg->payload) { free(msg); return; }

    memcpy(msg->payload, data, size);
    msg->size = size;
    msg->next = NULL;

    pthread_mutex_lock(&dest->lock); // si nécessaire
    if (dest->tail) {
        dest->tail->next = msg;
    } else {
        dest->head = msg;
    }
    dest->tail = msg;
    dest->len++;
    pthread_mutex_unlock(&dest->lock);
}

Message* mailbox_receive_message(Mailbox* self) {
    pthread_mutex_lock(&self->lock);
    Message* msg = self->head;
    if (msg) {
        self->head = msg->next;
        if (!self->head)
            self->tail = NULL;
        self->len--;
    }
    pthread_mutex_unlock(&self->lock);
    return msg; // à libérer après usage
}

Message* mailbox_peek_and_match(Mailbox* self, MessageMatchFn match, void* userdata) {
    pthread_mutex_lock(&self->lock);
    Message* msg = self->head;
    while (msg) {
        if (match(msg, userdata)) {
            // Retirer le message de la boîte aux lettres
            if (msg == self->head) {
                self->head = msg->next;
                if (!self->head)
                    self->tail = NULL;
            } else {
                Message* prev = self->head;
                while (prev && prev->next != msg) {
                    prev = prev->next;
                }
                if (prev) {
                    prev->next = msg->next;
                    if (!msg->next)
                        self->tail = prev;
                }
            }
            self->len--;
            pthread_mutex_unlock(&self->lock);
            return msg; // à libérer après usage
        }
        msg = msg->next;
    }
    pthread_mutex_unlock(&self->lock);
    return NULL; // aucun message correspondant trouvé
}