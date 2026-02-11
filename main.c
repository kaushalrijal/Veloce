#include "vcs.h"

#include <stdio.h>

int main(void)
{
    Session session = {0};
    RepoRecord opened_repo = {0};

    if (ensure_storage_ready() != 0)
    {
        (void)printf("Failed to initialize Veloce storage.\n");
        return 1;
    }

    load();

    while (verify_auth(&session))
    {
        if (!repo(&session, &opened_repo))
        {
            break;
        }
    }

    app_clear_screen();
    (void)printf("See you next time.\n");
    return 0;
}
