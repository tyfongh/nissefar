#ifndef DIFFUTIL_H
#define DIFFUTIL_H

#include <string>

std::string diff_csv(const std::string &olddata, const std::string &newdata,
                     int sheet_id, bool transpose = false);

#endif // DIFFUTIL_H
