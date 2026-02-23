#ifndef CALCULATIONSERVICE_H
#define CALCULATIONSERVICE_H

#include <dpp/dpp.h>
#include <string>

class CalculationService {
public:
  explicit CalculationService(dpp::cluster &bot);

  dpp::task<std::string> calculate_with_bc(const std::string &expression,
                                           int scale = 10) const;

private:
  dpp::cluster &bot;
};

#endif // CALCULATIONSERVICE_H
