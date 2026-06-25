#ifndef HERMES_IDENTITY_H
#define HERMES_IDENTITY_H

#include "status.h"
#include "types.h"

hermes_status hermes_identity_save(const char *path, const hermes_identity *identity);
hermes_status hermes_identity_load(const char *path, hermes_identity *identity);
hermes_status hermes_contact_save(const char *path, const hermes_contact *contact);
hermes_status hermes_contact_load(const char *path, hermes_contact *contact);

#endif

