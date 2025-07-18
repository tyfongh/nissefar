drop table if exists message cascade;

create table message
(
    message_id            serial
  , user_id               int references discord_user(user_id)
  , channel_id            int references channel(channel_id)
  , content               text
  , message_snowflake_id  bigint
  , reply_to_snowflake_id bigint
  , image_descriptions    text[]
);
