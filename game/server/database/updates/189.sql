create table account_reward_items --NO_IMPORT
(
        station_id number,
        item_id varchar2(255),
        date_claimed date,
        cluster_id number,
        character_id number,
        constraint pk_account_reward_items PRIMARY KEY (station_id, item_id)
);

grant select on account_reward_items to public;

create table account_reward_events --NO_IMPORT
(
        station_id number,
        event_id varchar2(255),
        date_consumed date,
        cluster_id number,
        character_id number,
        constraint pk_account_reward_events PRIMARY KEY (station_id, event_id)
);

grant select on account_reward_events to public;

alter table cluster_list add group_id int default 1 not null;

update version_number set version_number=189, min_version_number=189;
