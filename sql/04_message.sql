drop table if exists message cascade;

create table message
(
    message_id            serial primary key
  , user_id               int references discord_user(user_id)
  , channel_id            int references channel(channel_id)
  , content               text
  , message_snowflake_id  bigint
  , reply_to_snowflake_id bigint
  , image_descriptions    text[] default '{}'
  , created_at            timestamptz not null default '2025-01-01 00:00:00+00'
);
