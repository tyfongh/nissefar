#ifndef VIDEOSUMMARYSERVICE_H
#define VIDEOSUMMARYSERVICE_H

#include <Config.h>
#include <dpp/dpp.h>
#include <string>

class VideoSummaryService {
public:
  VideoSummaryService(const Config &config, dpp::cluster &bot);

  dpp::task<std::string> summarize_video(const std::string &url) const;

private:
  const Config &config;
  dpp::cluster &bot;
};

#endif // VIDEOSUMMARYSERVICE_H
