/* pde2c.c
   Simple .pde -> .c converter. Extracts top-level code, setup(), draw(), and functions,
   wraps them into a C file that includes "processing.h".
   Usage:
     gcc -O2 -std=c11 -o pde2c pde2c.c
     ./pde2c sketch.pde sketch.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

static int starts_with(const char *s, const char *p){
    while(*p){ if(*s!=*p) return 0; s++; p++; } return 1;
}

static void write_preamble(FILE *out, const char *basename){
    fprintf(out, "#define PROCESSING_IMPLEMENTATION\n");
    fprintf(out, "#include \"processing.h\"\n\n");
    fprintf(out, "/* Converted from %s.pde by pde2c.c */\n\n", basename);
}

/* Very simple parser: copy everything, but rename Processing's 'void setup()' and 'void draw()'
   signatures to C-compatible ones if needed and keep top-level statements (global vars).
   This is not a full Java parser; it works for typical PDE style sketches:
   - top-level variable declarations
   - function definitions (setup, draw, others)
   - no nested class definitions
*/
int main(int argc, char **argv){
    if(argc < 3){
        fprintf(stderr, "Usage: %s input.pde output.c\n", argv[0]);
        return 1;
    }
    const char *inpath = argv[1];
    const char *outpath = argv[2];

    FILE *in = fopen(inpath, "r");
    if(!in){ perror("open input"); return 2; }
    FILE *out = fopen(outpath, "w");
    if(!out){ perror("open output"); fclose(in); return 3; }

    /* derive basename for comment */
    const char *p = strrchr(inpath, '/');
    p = p ? p+1 : inpath;
    char basename[256];
    strncpy(basename, p, sizeof(basename)); basename[sizeof(basename)-1]=0;
    char *dot = strrchr(basename, '.');
    if(dot) *dot = '\0';

    write_preamble(out, basename);

    /* copy header includes from PDE if any (rare) and then rest */
    char line[4096];
    int in_block_comment = 0;
    while(fgets(line, sizeof(line), in)){
        /* strip Windows CR */
        size_t L = strlen(line); if(L && line[L-1]=='\r') line[L-1]=0;

        /* very small normalization: convert "void setup() {" and "void draw() {" to C prototypes
           and keep everything else verbatim. */
        char *trim = line;
        while(isspace((unsigned char)*trim)) trim++;
        if(starts_with(trim, "void setup(") || starts_with(trim, "int setup(") ||
           starts_with(trim, "void draw(")  || starts_with(trim, "int draw(")) {
            /* write exactly as-is: processing.h declares extern prototypes */
            fputs(line, out);
            continue;
        }
        /* If line uses println/println-like Processing functions, keep as-is (user may rely on them) */
        fputs(line, out);
    }

    /* ensure main is provided that calls processing_run */
    fprintf(out, "\nint main(void) {\n    return processing_run();\n}\n");

    fclose(in);
    fclose(out);
    return 0;
}
