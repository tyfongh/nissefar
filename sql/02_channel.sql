drop table if exists channel cascade;
create table channel
(
    channel_id    serial primary key
  , server_id     int references server(server_id)
  , channel_name  text
  , channel_snowflake_id  bigint
);
