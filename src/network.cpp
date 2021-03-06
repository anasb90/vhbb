#include "network.h"


#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/Exception.hpp>

#define MAX_FILE_LENGTH 20000

class ProgressClass {
public:
    ProgressClass(InfoProgress &progress) : m_progress(progress) {
    }

    int ProgressClassCallback(double dltotal, double dlnow, double ultotal, double ulnow) {
        m_progress.percent((float)(dlnow / dltotal) * 100);
        return CURLE_OK;
    }
private:
    InfoProgress &m_progress;
};

class WriterFileClass
{
public:
    WriterFileClass(std::string dest) {
        m_fd = sceIoOpen(dest.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);

        if (m_fd < 0)
            cURLpp::raiseException(std::runtime_error("Network: Couldn't write data"));
    }

    ~WriterFileClass() {
        if(m_fd >= 0) {
            sceIoClose(m_fd);
        }
    }

    size_t WriterFileClassCallback(char* ptr, size_t size, size_t nmemb)
    {
        int ret = sceIoWrite(m_fd, ptr, size*nmemb);
        if (ret < 0)
            cURLpp::raiseException(std::runtime_error("Network: Couldn't write data"));

        return ret;
    }

    int rewind() {
        return sceIoLseek(m_fd, 0, SCE_SEEK_SET);
    }

private:
    int m_fd = -1;
};

Network::Network()
{
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    SceNetInitParam netInitParam;
    int size = 1 * 1024 * 1024;
    netInitParam.memory = malloc(size);
    netInitParam.size = size;
    netInitParam.flags = 0;
    sceNetInit(&netInitParam);
    sceNetCtlInit();

    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);
    sceSslInit(300 * 1024);
    sceHttpInit(1 * 1024 * 1024);

    // Certificate verification for the API fails with SCE_HTTP_ERROR_SSL, SCE_HTTPS_ERROR_SSL_CN_CHECK
    sceHttpsDisableOption(SCE_HTTPS_FLAG_CN_CHECK);

    int ret = sceHttpCreateTemplate(VHBB_UA, SCE_HTTP_VERSION_1_1, SCE_TRUE);
    if (ret < 0)
        throw std::runtime_error("Network: Cannot create template");
    templateId_ = ret;

    cURLpp::initialize();
}

Network::~Network()
{
    cURLpp::terminate();

    sceHttpDeleteTemplate(templateId_);

    sceNetCtlTerm();
    sceNetTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);

    sceHttpTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTPS);

    sceSslEnd();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);

}

int Network::Download(std::string url, std::string dest, InfoProgress *progress)
{
    dbg_printf(DBG_DEBUG, "Downloading %s to %s", url.c_str(), dest.c_str());

    try {
        curlpp::Easy request;

        request.setOpt(new curlpp::options::Url(url));
        request.setOpt(new curlpp::options::UserAgent(VHBB_UA));
        request.setOpt(new curlpp::options::SslVerifyHost(0L));
        request.setOpt(new curlpp::options::SslVerifyPeer(false));

        request.setOpt(new curlpp::options::ConnectTimeout(10L));
        request.setOpt(new curlpp::options::FollowLocation(true));
        request.setOpt(new curlpp::options::MaxRedirs(8L));
        request.setOpt(new curlpp::options::FailOnError(true));

        using namespace std::placeholders;

        if (progress) {
            request.setOpt(new curlpp::options::NoProgress(false));

            ProgressClass mProgressClass(*progress);
            curlpp::types::ProgressFunctionFunctor progressFunctor = std::bind(&ProgressClass::ProgressClassCallback, &mProgressClass, _1, _2, _3, _4);

            request.setOpt(new curlpp::options::ProgressFunction(progressFunctor));
        }

        WriterFileClass mWriterChunk(dest);

        curlpp::types::WriteFunctionFunctor writeFunctor = std::bind(&WriterFileClass::WriterFileClassCallback, &mWriterChunk, _1, _2, _3);
        request.setOpt(new curlpp::options::WriteFunction(writeFunctor));

        for (unsigned int retries=1; retries <= 3; retries++) {
            try {
                request.perform();
                break;
            } catch (curlpp::RuntimeError &e) {
                if (retries == 3)
                    throw e;

                mWriterChunk.rewind();
                if(progress) progress->message("Retrying the download... (" + std::to_string(retries) + ")");
                sceKernelDelayThread(retries*500 * 1000);
                continue;
            }
        }

    } catch (curlpp::RuntimeError &e) {
        dbg_printf(DBG_ERROR, "cURLpp exception: ", e.what());
        throw std::runtime_error("Network: Cannot send request");
    }

    if(progress) progress->percent(100);

    return 0;
}

int Network::Download(std::string url, std::string dest, InfoProgress progress)
{
    return Download(url, dest, &progress);
}

InternetStatus Network::TestConnection()
{
    InternetStatus ret = INTERNET_STATUS_OK;
    int req = -1;
    int read = -1;
    int sendRes = -1;
    int res = -1;
    int statusCode = 0;
    uint64_t contentLength;
    char buf[4096] = {0};

    int conn = sceHttpCreateConnectionWithURL(templateId_, PORTAL_DETECT_URL, SCE_TRUE);
    if (conn < 0) {
        ret = INTERNET_STATUS_NO_INTERNET;
        goto clean;
    }

    req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, PORTAL_DETECT_URL, 0);
    if (req < 0) {
        ret = INTERNET_STATUS_NO_INTERNET;
        goto clean;
    }

    sendRes = sceHttpSendRequest(req, NULL, 0);

    
    res = sceHttpGetStatusCode(req, &statusCode);

    if (sendRes < 0 || res < 0 || statusCode != 200) {
        ret = INTERNET_STATUS_NO_INTERNET;
        goto clean;
    }
    
    res = sceHttpGetResponseContentLength(req, &contentLength);

    if (res >= 0)
        dbg_printf(DBG_DEBUG, "Content length: %lu", contentLength);

    
    read = sceHttpReadData(req, buf, sizeof(buf));
    if (res <= 0) {
        ret = INTERNET_STATUS_NO_INTERNET;
        goto clean;
    }

    buf[read] = '\0';

    if (strncmp(buf, PORTAL_DETECT_STR, strlen(PORTAL_DETECT_STR))) {
        ret = INTERNET_STATUS_HOTSPOT_PAGE;
        goto clean;
    }

    clean:
    if (req >= 0) {
        sceHttpDeleteRequest(req);
        sceHttpAbortRequest(req);
    }
    if (conn >= 0) sceHttpDeleteConnection(conn);

    return ret;
}