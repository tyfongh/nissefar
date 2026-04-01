#ifndef ADMINUTILS_H
#define ADMINUTILS_H

#include <Config.h>
#include <dpp/dpp.h>

// For slash command context — uses resolved permissions (includes channel overwrites)
inline bool is_admin(const dpp::interaction &interaction,
                     const Config &config) {
  const dpp::snowflake user_id = interaction.get_issuing_user().id;
  if (!config.owner_id.empty() && user_id.str() == config.owner_id)
    return true;
  try {
    const dpp::permission perms = interaction.get_resolved_permission(user_id);
    return perms.has(dpp::p_administrator) ||
           perms.has(dpp::p_manage_guild) ||
           perms.has(dpp::p_moderate_members) ||
           perms.has(dpp::p_manage_messages);
  } catch (...) {
    return false;
  }
}

// For message context — uses guild base permissions (role-level, no channel overwrites)
inline bool is_admin(const dpp::snowflake &user_id,
                     const dpp::guild_member &member,
                     const dpp::guild &guild,
                     const Config &config) {
  if (!config.owner_id.empty() && user_id.str() == config.owner_id)
    return true;
  const dpp::permission perms = guild.base_permissions(member);
  return perms.has(dpp::p_administrator) ||
         perms.has(dpp::p_manage_guild) ||
         perms.has(dpp::p_moderate_members) ||
         perms.has(dpp::p_manage_messages);
}

#endif // ADMINUTILS_H
