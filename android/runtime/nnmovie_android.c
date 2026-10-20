/* nnmovie_mf.c for Android, first version: the movie player keeps nnmovie.c's failing placeholder (as SSBU_MOVIE=0 does
 * on Windows): the game skips its movies (opening movie, fighter clips, final smash cut-ins).
 * TODO (next step of the port): movie::Extractor / movie::Decoder on AMediaExtractor / AMediaCodec. The SDK's movie library
 * is itself a port of Android's stagefright (see the header of runtime/nnmovie_mf.c), so the mapping is direct. */
#include <windows.h>
#include <stdio.h>
void register_movie_mf_imports(void) {
    printf("nn::movie: no movie decoder in this Android build yet: the game's movies are skipped\n");
}
