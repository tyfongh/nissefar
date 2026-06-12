alter table message
  add column if not exists sentiment jsonb,
  add column if not exists sentiment_model text,
  add column if not exists sentiment_evaluated_at timestamptz;
