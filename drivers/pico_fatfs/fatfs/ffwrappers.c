#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ffwrappers.h"
void normalize_path(const char *input, char *output) {
    const char *p = input;
    char **stack = malloc(256 * sizeof(char *)); // Allocate space for 256 tokens
    int stack_index = 0;

    if (!stack) {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    // Process the input path
    while (*p != '\0') {
        // Skip multiple slashes
        if (*p == '/') {
            p++;
            continue;
        }

        // Extract the next token
        char token[256];
        int token_len = 0;
        while (*p != '/' && *p != '\0') {
            if (token_len < 255) { // Ensure token does not exceed 255 characters
                token[token_len++] = *p++;
            } else {
                p++; // Skip excess characters
            }
        }
        token[token_len] = '\0';

        // Handle special tokens
        if (strcmp(token, ".") == 0) {
            continue; // Skip current directory
        } else if (strcmp(token, "..") == 0) {
            if (stack_index > 0) {
                free(stack[--stack_index]); // Pop the stack
            }
        } else {
            // Push the token onto the stack
            stack[stack_index] = malloc(strlen(token) + 1);
            if (stack[stack_index]) {
                strcpy(stack[stack_index++], token);
            }
        }
    }

    // Reconstruct the normalized path
    *output = '\0';
    if (stack_index == 0) {
        strcpy(output, "/");
    } else {
        for (int i = 0; i < stack_index; i++) {
            strcat(output, "/");
            strcat(output, stack[i]);
            free(stack[i]); // Free each token
        }
    }

    free(stack); // Free the stack
}

static TCHAR current_dir[FF_MAX_LFN] = "/"; // Static variable to store the current directory
// Wrapper function for f_chdir that tracks the current directory
FRESULT my_chdir(const TCHAR *path) {
    
    TCHAR temp[FF_MAX_LFN];

    // Check if path is absolute
    if (path[0] == '/') {
        strncpy(temp, path, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0'; // Ensure null termination
    } else {
        // If the path is relative, append it to the current directory
        snprintf(temp, sizeof(temp), "%s/%s", current_dir, path);
    }

    // Normalize the resulting path (resolve "..", ".", and duplicate slashes)
    TCHAR normalized[FF_MAX_LFN];
    // A function to normalize the path should be implemented here.
    // For simplicity, assume normalize_path(temp, normalized) is used.
    normalize_path(temp, normalized);
    FRESULT fr;
    // Call the actual f_chdir function
    if ((fr = f_chdir(normalized)) == FR_OK) {
        // Update the static variable if f_chdir succeeds
        strncpy(current_dir, normalized, sizeof(current_dir) - 1);
        current_dir[sizeof(current_dir) - 1] = '\0'; // Ensure null termination
    }

    return fr; 
}

// Function to retrieve the current directory
const FRESULT my_getcwd(TCHAR *buffer, UINT len) {
    if (len < strlen(current_dir) + 1) {
        return FR_INVALID_PARAMETER; // Buffer too small
    }
    strncpy(buffer, current_dir, len);
    return FR_OK;
}