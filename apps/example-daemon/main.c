#include <stdio.h>
#include <unistd.h>

/*
 * Placeholder application, standing in for whatever real SpeakerPoint
 * app(s) end up living under apps/. Its only job is to prove the
 * apps/<name> + br-external/package/<name>/ wiring builds, installs,
 * and starts correctly end to end.
 */
int main(void)
{
    for (;;) {
        printf("speakerpoint example-daemon: app framework online\n");
        fflush(stdout);
        sleep(60);
    }
    return 0;
}
