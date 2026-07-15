#include "app.h"

int main(int argc, char **argv)
{
    return app_run(argc > 1 ? argv[1] : 0);
}
