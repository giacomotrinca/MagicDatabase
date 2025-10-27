#include "utils.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <iostream>
#include <cstring>

bool ensure_data_dir_exists(const std::string& path) {
    struct stat st = {0};
    if (stat(path.c_str(), &st) == -1) {
        if (mkdir(path.c_str(), 0755) == -1 && errno != EEXIST) {
            std::cerr << "Impossibile creare la cartella '" << path << "': " << strerror(errno) << std::endl;
            return false;
        }
    }
    return true;
}
