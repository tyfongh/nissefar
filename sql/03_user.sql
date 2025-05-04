drop table if exists discord_user cascade;
create table discord_user
(
    user_id            serial primary key
  , user_name          text
  , user_snowflake_id  bigint
);
