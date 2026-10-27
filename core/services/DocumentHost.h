#pragma once
namespace phthongos::services {
// Transitional callback ABI. Handles own the only mutable document; Rust only
// owns a handle plus immutable analysis inputs and diagnostic cache metadata.
char* documentRequest(const char* command,const char* json) noexcept;
void documentFree(char*) noexcept;
}
