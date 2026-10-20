#include "documents/pdf_generator.hpp"

#include <windows.h>

#include <cstdio>
#include <fstream>

#include "runtime/edge_launcher.hpp"
#include "runtime/log.hpp"
#include "runtime/paths.hpp"

namespace fs = std::filesystem;

namespace wcatalog {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

bool starts_with_pdf_header(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    char head[4] = {};
    in.read(head, 4);
    return in.gcount() == 4 && head[0] == '%' && head[1] == 'P' && head[2] == 'D' && head[3] == 'F';
}

// Cierra el trabajo (y con él, todo el árbol de procesos de Edge) al salir de ámbito.
struct JobGuard {
    HANDLE job = nullptr;
    ~JobGuard() {
        if (job) CloseHandle(job);
    }
};

}  // namespace

std::string path_to_file_url(const fs::path& path) {
    const std::string utf8 = to_utf8(path.lexically_normal());
    std::string url = "file:///";
    for (unsigned char c : utf8) {
        if (c == '\\') {
            url += '/';
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                   c == '_' || c == '~' || c == '/' || c == ':') {
            url += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            url += buf;
        }
    }
    return url;
}

EdgePdfGenerator::EdgePdfGenerator() : EdgePdfGenerator(Options{}) {}

EdgePdfGenerator::EdgePdfGenerator(Options options) : options_(std::move(options)) {
    if (!options_.find_edge) options_.find_edge = [] { return find_edge(); };
    if (options_.profile_dir.empty()) options_.profile_dir = edge_headless_profile_dir();
}

PdfResult EdgePdfGenerator::generate(const fs::path& html, const fs::path& pdf, int pages) {
    PdfResult result;
    const std::optional<fs::path> edge = options_.find_edge();
    if (!edge) {
        result.reason = "edge_no_encontrado";
        result.message = "No se encontró Microsoft Edge en este equipo";
        return result;
    }

    std::error_code ec;
    fs::remove(pdf, ec);
    fs::create_directories(options_.profile_dir, ec);
    fs::create_directories(pdf.parent_path(), ec);

    std::wstring cmd;
    if (options_.command_override) {
        cmd = options_.command_override(*edge, html, pdf);
    } else {
        cmd = L"\"" + edge->wstring() + L"\" --headless --disable-gpu --no-first-run --no-default-browser-check "
              L"--no-pdf-header-footer \"--user-data-dir=" + options_.profile_dir.wstring() +
              L"\" \"--print-to-pdf=" + pdf.wstring() + L"\" " + widen(path_to_file_url(html));
    }

    JobGuard job;
    job.job = CreateJobObjectW(nullptr, nullptr);
    if (job.job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job.job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // suspendido hasta asignarlo al trabajo, para que sus procesos hijos queden dentro
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        nullptr, &si, &pi)) {
        result.reason = "fallo_edge";
        result.message = "No se pudo iniciar Edge (error " + std::to_string(GetLastError()) + ")";
        return result;
    }
    if (job.job) AssignProcessToJobObject(job.job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    const int seconds = options_.base_timeout_seconds + options_.per_page_seconds * (pages > 0 ? pages : 1);
    const DWORD wait = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(seconds) * 1000);
    if (wait == WAIT_TIMEOUT) {
        if (job.job) TerminateJobObject(job.job, 1);
        else TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        fs::remove(pdf, ec);
        result.reason = "tiempo_agotado";
        result.message = "Edge tardó más de " + std::to_string(seconds) + " segundos en generar el PDF";
        log().warn("PDF: tiempo agotado (" + std::to_string(seconds) + " s) para " + to_utf8(html));
        return result;
    }

    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (code != 0) {
        fs::remove(pdf, ec);
        result.reason = "fallo_edge";
        result.message = "Edge terminó con el código " + std::to_string(code);
        log().warn("PDF: " + result.message + " para " + to_utf8(html));
        return result;
    }
    if (!fs::is_regular_file(pdf, ec) || fs::file_size(pdf, ec) == 0 || !starts_with_pdf_header(pdf)) {
        fs::remove(pdf, ec);
        result.reason = "fallo_edge";
        result.message = "Edge terminó, pero no produjo un PDF válido";
        log().warn("PDF: " + result.message + " para " + to_utf8(html));
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace wcatalog
