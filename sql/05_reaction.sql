drop table if exists reaction cascade;

create table reaction
(
    reaction_id           serial primary key
  , message_id            int references message(message_id)
  , user_id               int references discord_user(user_id)
  , reaction              text
);
