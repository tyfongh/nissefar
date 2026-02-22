#ifndef WEBPAGESERVICE_H
#define WEBPAGESERVICE_H

#include <dpp/dpp.h>
#include <string>

class WebPageService {
public:
  explicit WebPageService(dpp::cluster &bot);

  dpp::task<std::string> fetch_webpage_text(const std::string &url) const;

private:
  dpp::cluster &bot;
};

#endif // WEBPAGESERVICE_H
