#pragma once

#include "logger.h"
#include <eh.h>
#include <exception>

namespace ever {
    namespace hooking {
        class SehException : public std::exception {
        public:
            explicit SehException(unsigned int code) : code_(code) {}
            
            const char* what() const noexcept override { 
                return "SEH exception occurred during hook operation"; 
            }
            
            unsigned int getCode() const noexcept { 
                return code_; 
            }

        private:
            unsigned int code_;
        };

        inline void __cdecl sehTranslator(unsigned int code, EXCEPTION_POINTERS*) {
            throw SehException(code);
        }

        class SehTranslatorGuard {
        public:
            explicit SehTranslatorGuard(_se_translator_function translator) 
                : previous_(_set_se_translator(translator)) {}
            
            ~SehTranslatorGuard() { 
                _set_se_translator(previous_); 
            }

            SehTranslatorGuard(const SehTranslatorGuard&) = delete;
            SehTranslatorGuard& operator=(const SehTranslatorGuard&) = delete;

        private:
            _se_translator_function previous_;
        };

    }
}
