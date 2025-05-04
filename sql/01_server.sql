drop table if exists server cascade;
create table server
(
    server_id            serial primary key
  , server_name          text
  , server_snowflake_id  bigint
);
