/* kernel.c - 미래 OS 통합 커널 예제 */
/* 포함된 명령어: 
   cd, cd.., md, rm, pwd, ls/dir, cat, echo, clear, help, history,
   shred, linkfile, touch, cp, mv, find, execbin, execelf
*/

/* ======================= 기본 타입 및 문자열/메모리 함수 ======================= */
typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;
typedef uint32_t       size_t;

size_t kstrlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int kstrcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int kstrncmp(const char* s1, const char* s2, size_t n) {
    while (n-- && *s1 && *s2) {
        if (*s1 != *s2)
            return *(unsigned char*)s1 - *(unsigned char*)s2;
        s1++; s2++;
    }
    return 0;
}

char* kstrncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    return dest;
}

char* kstrcat(char* dest, const char* src) {
    int i = 0;
    while (dest[i]) i++;
    int j = 0;
    while (src[j]) {
        dest[i+j] = src[j];
        j++;
    }
    dest[i+j] = '\0';
    return dest;
}

void* kmemcpy(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

/* 문자열 내에 특정 패턴 포함 여부 */
int kstrcontains(const char* str, const char* substr) {
    if (!*substr) return 1;
    for (int i = 0; str[i] != '\0'; i++) {
        int j = 0;
        while (str[i+j] && substr[j] && (str[i+j] == substr[j])) {
            j++;
        }
        if (substr[j] == '\0') return 1;
    }
    return 0;
}

/* ======================= VGA 출력 관련 ======================= */
volatile uint16_t* vga_buffer = (uint16_t*)0xb8000;
int vga_cursor = 0;  // 전체 화면 출력용 커서

void kprint(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (c == '\n')
            vga_cursor = ((vga_cursor / 80) + 1) * 80;
        else
            vga_buffer[vga_cursor++] = (uint16_t)c | (0x07 << 8);
    }
}

void kprintln(const char* str) {
    kprint(str);
    kprint("\n");
}

void kprint_dec(int num) {
    char buffer[16];
    int i = 0;
    if (num == 0) {
        buffer[i++] = '0';
    } else {
        int n = num;
        if (n < 0) {
            kprint("-");
            n = -n;
        }
        while (n > 0) {
            buffer[i++] = '0' + (n % 10);
            n /= 10;
        }
        for (int j = 0; j < i/2; j++) {
            char tmp = buffer[j];
            buffer[j] = buffer[i-1-j];
            buffer[i-1-j] = tmp;
        }
    }
    buffer[i] = '\0';
    kprint(buffer);
}

/* ======================= 인라인 포트 I/O (키보드, PIC 등) ======================= */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t data) {
    asm volatile ("outb %0, %1" : : "a"(data), "Nd"(port));
}

/* ======================= CLI 관련 ======================= */
#define CLI_BUFFER_SIZE 256
char cli_buffer[CLI_BUFFER_SIZE] = {0};
int cli_length = 0;
int cli_cursor = 0;

void update_cursor() {
    uint16_t pos = (24 * 80) + 3 + cli_cursor;
    outb(0x3D4, 14);
    outb(0x3D5, pos >> 8);
    outb(0x3D4, 15);
    outb(0x3D5, pos & 0xFF);
}

void update_cli_display() {
    volatile uint16_t* vga = (volatile uint16_t*)0xb8000;
    int line = 24;
    int offset = line * 80;
    for (int i = 0; i < 80; i++) vga[offset + i] = ' ' | (0x07 << 8);
    const char* prompt = ">> ";
    for (int i = 0; i < 3; i++) vga[offset + i] = prompt[i] | (0x07 << 8);
    for (int i = 0; i < cli_length; i++)
        vga[offset + 3 + i] = cli_buffer[i] | (0x07 << 8);
    update_cursor();
}

/* clear 명령어: 화면 전체 지우기 */
void clear_screen() {
    for (int i = 0; i < 80*25; i++) {
        vga_buffer[i] = ' ' | (0x07 << 8);
    }
    vga_cursor = 0;
    update_cli_display();
}

/* ======================= 간단 토큰화 함수 ======================= */
int tokenize(char* input, char* tokens[], int max_tokens) {
    int count = 0, in_token = 0;
    for (char* p = input; *p; p++) {
        if (*p == ' ' || *p == '\t') {
            *p = '\0';
            in_token = 0;
        } else {
            if (!in_token) {
                if (count < max_tokens)
                    tokens[count++] = p;
                in_token = 1;
            }
        }
    }
    return count;
}

/* ======================= 기억FS (간단한 계층형 파일 시스템) ======================= */
#define MAX_FILES        10
#define MAX_FILENAME_LEN 64
#define MAX_FILE_SIZE    1024

typedef struct {
    char name[MAX_FILENAME_LEN];  /* 전체 경로 (예: "/dir/file.txt") */
    char content[MAX_FILE_SIZE];
    size_t size;
    int used;
    int is_directory;  /* 0: 파일, 1: 디렉토리 */
} File;

typedef struct {
    File files[MAX_FILES];
    int file_count;
} MemoryFS;

MemoryFS fs;
char current_directory[256] = "/";

void init_fs() {
    for (int i = 0; i < MAX_FILES; i++) fs.files[i].used = 0;
    fs.file_count = 0;
    /* 루트 디렉토리 생성 */
    fs.files[0].used = 1;
    fs.files[0].is_directory = 1;
    kstrncpy(fs.files[0].name, "/", MAX_FILENAME_LEN);
    fs.files[0].size = 0;
    fs.file_count = 1;
}

int fs_find(const char* path) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.files[i].used && kstrcmp(fs.files[i].name, path) == 0)
            return i;
    }
    return -1;
}

/* 현재 디렉토리와 상대 경로를 결합 */
void build_full_path(const char* relative, char* out, int out_size) {
    if (kstrcmp(current_directory, "/") == 0) {
        kstrncpy(out, "/", out_size);
        kstrcat(out, relative);
    } else {
        kstrncpy(out, current_directory, out_size);
        kstrcat(out, "/");
        kstrcat(out, relative);
    }
}

/* 파일 생성 (일반 파일) */
int fs_create_file(const char* name) {
    char full_path[MAX_FILENAME_LEN];
    build_full_path(name, full_path, MAX_FILENAME_LEN);
    if (fs_find(full_path) != -1) {
       kprintln("파일이 이미 존재합니다.");
       return -1;
    }
    for (int i = 0; i < MAX_FILES; i++) {
        if (!fs.files[i].used) {
            fs.files[i].used = 1;
            fs.files[i].is_directory = 0;
            kstrncpy(fs.files[i].name, full_path, MAX_FILENAME_LEN);
            fs.files[i].size = 0;
            fs.files[i].content[0] = '\0';
            fs.file_count++;
            return i;
        }
    }
    kprintln("파일 생성 실패: 슬롯 부족");
    return -1;
}

/* 디렉토리 생성 */
int fs_create_directory(const char* name) {
    char full_path[MAX_FILENAME_LEN];
    build_full_path(name, full_path, MAX_FILENAME_LEN);
    if (fs_find(full_path) != -1) {
       kprintln("디렉토리가 이미 존재합니다.");
       return -1;
    }
    for (int i = 0; i < MAX_FILES; i++) {
        if (!fs.files[i].used) {
            fs.files[i].used = 1;
            fs.files[i].is_directory = 1;
            kstrncpy(fs.files[i].name, full_path, MAX_FILENAME_LEN);
            fs.files[i].size = 0;
            fs.file_count++;
            return i;
        }
    }
    kprintln("디렉토리 생성 실패: 슬롯 부족");
    return -1;
}

/* 파일/디렉토리 삭제 */
int fs_delete(const char* path) {
    int idx = fs_find(path);
    if (idx == -1) {
        kprintln("삭제할 파일/디렉토리가 존재하지 않습니다.");
        return -1;
    }
    if (fs.files[idx].is_directory) {
       int path_len = kstrlen(path);
       for (int i = 0; i < MAX_FILES; i++) {
         if (fs.files[i].used && i != idx && kstrlen(fs.files[i].name) > path_len) {
             if (kstrncmp(fs.files[i].name, path, path_len) == 0 &&
                 fs.files[i].name[path_len] == '/') {
                 kprintln("디렉토리가 비어있지 않습니다.");
                 return -1;
             }
         }
       }
    }
    fs.files[idx].used = 0;
    fs.file_count--;
    return 0;
}

/* 현재 디렉토리 항목 목록 출력 */
void fs_list_directory() {
    int current_len = kstrlen(current_directory);
    kprintln("=== Directory Listing ===");
    for (int i = 0; i < MAX_FILES; i++) {
        if (!fs.files[i].used) continue;
        if (kstrncmp(fs.files[i].name, current_directory, current_len) == 0) {
            char* remainder = fs.files[i].name + current_len;
            if (kstrcmp(current_directory, "/") != 0) {
                if (remainder[0] != '/') continue;
                remainder++;
            }
            int is_direct = 1;
            for (int j = 0; remainder[j] != '\0'; j++) {
                if (remainder[j] == '/') { is_direct = 0; break; }
            }
            if (is_direct && kstrlen(remainder) > 0) {
                kprint(remainder);
                if (fs.files[i].is_directory) {
                    kprintln(" <DIR>");
                } else {
                    kprint(" ");
                    kprint_dec((int)fs.files[i].size);
                    kprintln(" bytes");
                }
            }
        }
    }
}

/* 디렉토리 변경: ".."는 상위, 그 외는 하위 디렉토리 이동 */
int fs_change_directory(const char* path) {
    if (kstrcmp(path, "..") == 0) {
        if (kstrcmp(current_directory, "/") == 0) {
            kprintln("이미 루트 디렉토리입니다.");
            return -1;
        }
        int len = kstrlen(current_directory);
        if (current_directory[len-1] == '/') current_directory[len-1] = '\0';
        int i;
        for (i = len - 1; i >= 0; i--) {
            if (current_directory[i] == '/') break;
        }
        if (i == 0) {
            current_directory[0] = '/';
            current_directory[1] = '\0';
        } else {
            current_directory[i] = '\0';
        }
        return 0;
    } else {
       char new_path[MAX_FILENAME_LEN];
       if (kstrcmp(current_directory, "/") == 0) {
           kstrncpy(new_path, "/", MAX_FILENAME_LEN);
           kstrcat(new_path, path);
       } else {
           kstrncpy(new_path, current_directory, MAX_FILENAME_LEN);
           kstrcat(new_path, "/");
           kstrcat(new_path, path);
       }
       int idx = fs_find(new_path);
       if (idx == -1 || !fs.files[idx].is_directory) {
           kprintln("디렉토리가 존재하지 않습니다.");
           return -1;
       }
       kstrncpy(current_directory, new_path, 256);
       return 0;
    }
}

/* 파일 복사 */
int fs_copy_file(const char* source, const char* destination) {
    char source_full[MAX_FILENAME_LEN];
    build_full_path(source, source_full, MAX_FILENAME_LEN);
    int src_idx = fs_find(source_full);
    if (src_idx == -1 || fs.files[src_idx].is_directory) {
       kprintln("소스 파일이 존재하지 않거나 디렉토리입니다.");
       return -1;
    }
    char dest_full[MAX_FILENAME_LEN];
    build_full_path(destination, dest_full, MAX_FILENAME_LEN);
    if (fs_find(dest_full) != -1) {
       kprintln("대상 파일이 이미 존재합니다.");
       return -1;
    }
    int new_idx = fs_create_file(destination);
    if (new_idx < 0) return -1;
    kmemcpy(fs.files[new_idx].content, fs.files[src_idx].content, fs.files[src_idx].size);
    fs.files[new_idx].size = fs.files[src_idx].size;
    return 0;
}

/* 파일/디렉토리 이동 (이름 변경) */
int fs_move_file(const char* source, const char* destination) {
    char source_full[MAX_FILENAME_LEN];
    build_full_path(source, source_full, MAX_FILENAME_LEN);
    int src_idx = fs_find(source_full);
    if (src_idx == -1) {
        kprintln("소스 파일/디렉토리가 존재하지 않습니다.");
        return -1;
    }
    char dest_full[MAX_FILENAME_LEN];
    build_full_path(destination, dest_full, MAX_FILENAME_LEN);
    if (fs_find(dest_full) != -1) {
        kprintln("대상 파일/디렉토리가 이미 존재합니다.");
        return -1;
    }
    kstrncpy(fs.files[src_idx].name, dest_full, MAX_FILENAME_LEN);
    return 0;
}

/* touch: 파일이 없으면 생성, 있으면 그대로 둠 */
int fs_touch(const char* name) {
    char full_path[MAX_FILENAME_LEN];
    build_full_path(name, full_path, MAX_FILENAME_LEN);
    int idx = fs_find(full_path);
    if (idx != -1) return idx;
    return fs_create_file(name);
}

/* linkfile: 하드 링크 (내용 복사 방식) */
int fs_link_file(const char* source, const char* linkname) {
    char source_full[MAX_FILENAME_LEN];
    build_full_path(source, source_full, MAX_FILENAME_LEN);
    int src_idx = fs_find(source_full);
    if (src_idx == -1 || fs.files[src_idx].is_directory) {
       kprintln("소스 파일이 존재하지 않거나 디렉토리입니다.");
       return -1;
    }
    int link_idx = fs_create_file(linkname);
    if (link_idx < 0) return -1;
    kmemcpy(fs.files[link_idx].content, fs.files[src_idx].content, fs.files[src_idx].size);
    fs.files[link_idx].size = fs.files[src_idx].size;
    return 0;
}

/* shred: 파일 내용을 0으로 덮어쓰고 삭제 */
int fs_shred_file(const char* name) {
    char full_path[MAX_FILENAME_LEN];
    build_full_path(name, full_path, MAX_FILENAME_LEN);
    int idx = fs_find(full_path);
    if (idx == -1 || fs.files[idx].is_directory) {
       kprintln("파일이 존재하지 않거나 디렉토리입니다.");
       return -1;
    }
    for (int i = 0; i < fs.files[idx].size; i++)
        fs.files[idx].content[i] = 0;
    return fs_delete(full_path);
}

/* ======================= ELF 로더 및 Raw binary 실행 ======================= */
#define PT_LOAD 1

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

/* ELF 파일 로더: loadable 세그먼트를 메모리로 복사한 후 엔트리 포인트로 점프 */
void exec_elf(char* elf_data, size_t size) {
    if (size < sizeof(Elf32_Ehdr)) {
        kprintln("ELF 파일 크기가 너무 작습니다.");
        return;
    }
    Elf32_Ehdr* header = (Elf32_Ehdr*)elf_data;
    if (!(header->e_ident[0] == 0x7F &&
          header->e_ident[1] == 'E' &&
          header->e_ident[2] == 'L' &&
          header->e_ident[3] == 'F')) {
        kprintln("유효한 ELF 파일이 아닙니다.");
        return;
    }
    Elf32_Phdr* phdr = (Elf32_Phdr*)(elf_data + header->e_phoff);
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            kmemcpy((void*)phdr[i].p_vaddr, elf_data + phdr[i].p_offset, phdr[i].p_filesz);
            if (phdr[i].p_memsz > phdr[i].p_filesz) {
                for (uint32_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++)
                    ((char*)phdr[i].p_vaddr)[j] = 0;
            }
        }
    }
    kprint("Jumping to ELF entry point: ");
    kprint_dec(header->e_entry);
    kprintln("");
    void (*entry)() = (void (*)())header->e_entry;
    entry();
}

/* Raw binary 실행: 파일 내용을 0x200000번지로 복사 후 점프 */
void exec_bin(char* bin, size_t size) {
    char* exec_addr = (char*)0x200000;
    kmemcpy(exec_addr, bin, size);
    kprint("Jumping to raw binary at: ");
    kprint_dec((int)exec_addr);
    kprintln("");
    void (*entry)() = (void (*)())exec_addr;
    entry();
}

/* ======================= 명령어 히스토리 ======================= */
#define MAX_HISTORY 10
char command_history[MAX_HISTORY][CLI_BUFFER_SIZE];
int history_count = 0;

/* ======================= CLI 명령어 처리 ======================= */
void process_command() {
    /* 히스토리에 저장 (빈 명령어는 저장하지 않음) */
    if (cli_length > 0 && cli_buffer[0] != '\0') {
        if (history_count < MAX_HISTORY) {
            kstrncpy(command_history[history_count], cli_buffer, CLI_BUFFER_SIZE);
            history_count++;
        } else {
            for (int i = 1; i < MAX_HISTORY; i++)
                kstrncpy(command_history[i-1], command_history[i], CLI_BUFFER_SIZE);
            kstrncpy(command_history[MAX_HISTORY-1], cli_buffer, CLI_BUFFER_SIZE);
        }
    }
    
    char* tokens[10];
    int token_count = tokenize(cli_buffer, tokens, 10);
    if (token_count == 0) return;
    
    if (kstrcmp(tokens[0], "cd") == 0) {
         if (token_count < 2)
             kprintln("사용법: cd <directory>");
         else
             fs_change_directory(tokens[1]);
    } else if (kstrcmp(tokens[0], "cd..") == 0) {
         fs_change_directory("..");
    } else if (kstrcmp(tokens[0], "md") == 0) {
         if (token_count < 2)
             kprintln("사용법: md <directory>");
         else
             fs_create_directory(tokens[1]);
    } else if (kstrcmp(tokens[0], "rm") == 0) {
         if (token_count < 2)
             kprintln("사용법: rm <file_or_directory>");
         else
             fs_delete(tokens[1]);
    } else if (kstrcmp(tokens[0], "pwd") == 0) {
         kprintln(current_directory);
    } else if (kstrcmp(tokens[0], "ls") == 0 || kstrcmp(tokens[0], "dir") == 0) {
         fs_list_directory();
    } else if (kstrcmp(tokens[0], "cat") == 0) {
         if (token_count < 2)
             kprintln("사용법: cat <file>");
         else {
             char full_path[MAX_FILENAME_LEN];
             build_full_path(tokens[1], full_path, MAX_FILENAME_LEN);
             int idx = fs_find(full_path);
             if (idx == -1 || fs.files[idx].is_directory)
                 kprintln("파일이 존재하지 않거나 디렉토리입니다.");
             else {
                 kprintln("File content:");
                 kprintln(fs.files[idx].content);
             }
         }
    } else if (kstrcmp(tokens[0], "echo") == 0) {
         for (int i = 1; i < token_count; i++) {
             kprint(tokens[i]);
             if (i < token_count - 1)
                 kprint(" ");
         }
         kprintln("");
    } else if (kstrcmp(tokens[0], "clear") == 0) {
         clear_screen();
    } else if (kstrcmp(tokens[0], "help") == 0) {
         kprintln("Available commands:");
         kprintln("cd <dir>       - change directory");
         kprintln("cd..           - go to parent directory");
         kprintln("md <dir>       - make directory");
         kprintln("rm <file/dir>  - remove file/directory");
         kprintln("pwd            - print working directory");
         kprintln("ls or dir      - list directory contents");
         kprintln("cat <file>     - display file content");
         kprintln("echo <text>    - print text");
         kprintln("clear          - clear the screen");
         kprintln("history        - show command history");
         kprintln("shred <file>   - secure delete file");
         kprintln("linkfile <src> <link> - create hard link");
         kprintln("touch <file>   - create empty file");
         kprintln("cp <src> <dest> - copy file");
         kprintln("mv <src> <dest> - move/rename file");
         kprintln("find <pattern> - search files");
         kprintln("execbin <file> - execute raw binary file");
         kprintln("execelf <file> - execute ELF file");
    } else if (kstrcmp(tokens[0], "history") == 0) {
         kprintln("Command History:");
         for (int i = 0; i < history_count; i++)
             kprintln(command_history[i]);
    } else if (kstrcmp(tokens[0], "shred") == 0) {
         if (token_count < 2)
             kprintln("사용법: shred <file>");
         else
             fs_shred_file(tokens[1]);
    } else if (kstrcmp(tokens[0], "linkfile") == 0) {
         if (token_count < 3)
             kprintln("사용법: linkfile <source> <linkname>");
         else
             fs_link_file(tokens[1], tokens[2]);
    } else if (kstrcmp(tokens[0], "touch") == 0) {
         if (token_count < 2)
             kprintln("사용법: touch <file>");
         else
             fs_touch(tokens[1]);
    } else if (kstrcmp(tokens[0], "cp") == 0) {
         if (token_count < 3)
             kprintln("사용법: cp <source> <destination>");
         else
             fs_copy_file(tokens[1], tokens[2]);
    } else if (kstrcmp(tokens[0], "mv") == 0) {
         if (token_count < 3)
             kprintln("사용법: mv <source> <destination>");
         else
             fs_move_file(tokens[1], tokens[2]);
    } else if (kstrcmp(tokens[0], "find") == 0) {
         if (token_count < 2)
             kprintln("사용법: find <pattern>");
         else {
             int found = 0;
             for (int i = 0; i < MAX_FILES; i++) {
                 if (fs.files[i].used && kstrcontains(fs.files[i].name, tokens[1])) {
                     kprintln(fs.files[i].name);
                     found = 1;
                 }
             }
             if (!found)
                 kprintln("No matching files found.");
         }
    } else if (kstrcmp(tokens[0], "execbin") == 0) {
         if (token_count < 2)
             kprintln("사용법: execbin <file>");
         else {
             char full_path[MAX_FILENAME_LEN];
             build_full_path(tokens[1], full_path, MAX_FILENAME_LEN);
             int idx = fs_find(full_path);
             if (idx == -1 || fs.files[idx].is_directory)
                 kprintln("File not found or is a directory.");
             else {
                 kprintln("Loading raw binary...");
                 exec_bin(fs.files[idx].content, fs.files[idx].size);
             }
         }
    } else if (kstrcmp(tokens[0], "execelf") == 0) {
         if (token_count < 2)
             kprintln("사용법: execelf <file>");
         else {
             char full_path[MAX_FILENAME_LEN];
             build_full_path(tokens[1], full_path, MAX_FILENAME_LEN);
             int idx = fs_find(full_path);
             if (idx == -1 || fs.files[idx].is_directory)
                 kprintln("File not found or is a directory.");
             else {
                 kprintln("Loading ELF file...");
                 exec_elf(fs.files[idx].content, fs.files[idx].size);
             }
         }
    } else {
         kprintln("알 수 없는 명령어");
    }
    cli_length = 0;
    cli_cursor = 0;
    cli_buffer[0] = '\0';
    update_cli_display();
}

/* ======================= 키보드 드라이버 ======================= */
char scancode_map[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b','\t',
   'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s',
   'd','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v',
   'b','n','m',',','.','/', 0, '*', 0, ' ', 0, 0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0
};

void process_keyboard(uint8_t scancode) {
    static int extended_flag = 0;
    if (scancode == 0xE0) { extended_flag = 1; return; }
    if (extended_flag) {
        extended_flag = 0;
        if (scancode == 0x4B && cli_cursor > 0) cli_cursor--;
        else if (scancode == 0x4D && cli_cursor < cli_length) cli_cursor++;
        update_cli_display();
        return;
    }
    if (scancode & 0x80) return;
    char key = scancode_map[scancode];
    if (key == '\n') {
        process_command();
    } else if (key == '\b') {
        if (cli_cursor > 0 && cli_length > 0) {
            for (int i = cli_cursor - 1; i < cli_length - 1; i++)
                cli_buffer[i] = cli_buffer[i+1];
            cli_length--; cli_cursor--;
            cli_buffer[cli_length] = '\0';
        }
    } else if (key != 0) {
        if (cli_length < CLI_BUFFER_SIZE - 1) {
            for (int i = cli_length; i > cli_cursor; i--)
                cli_buffer[i] = cli_buffer[i-1];
            cli_buffer[cli_cursor] = key;
            cli_length++; cli_cursor++;
        }
    }
    update_cli_display();
}

/* ======================= PIC, IDT 및 인터럽트 초기화 ======================= */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

void set_idt_gate(int num, uint32_t handler) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = 0x08;
    idt[num].zero = 0;
    idt[num].type_attr = 0x8E;
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
}

void init_idt() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;
    for (int i = 0; i < 256; i++) set_idt_gate(i, 0);
    extern void keyboard_interrupt_handler();
    set_idt_gate(0x21, (uint32_t)keyboard_interrupt_handler);
    asm volatile ("lidt (%0)" : : "r" (&idtp));
}

void init_pic() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0x0);
    outb(0xA1, 0x0);
}

__attribute__((interrupt))
void keyboard_interrupt_handler(void* frame) {
    uint8_t scancode = inb(0x60);
    process_keyboard(scancode);
    outb(0x20, 0x20);
}

/* ======================= 미래 OS 커널 메인 ======================= */
void kernel_main(void) {
    kprintln("미래 Kernel started!");
    init_fs();           // 기억FS 초기화 (루트 디렉토리 생성)
    init_pic();
    init_idt();
    asm volatile ("sti"); // 인터럽트 활성화
    update_cli_display(); // CLI 초기 화면 출력
    while (1) { asm volatile ("hlt"); }
}
