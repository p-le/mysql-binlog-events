/* Copyright (c) 2014, 2015, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "binary_log_types.h"

#include "statement_events.h"
#include <iostream>
using namespace std;
#include <algorithm>
#include <cstdio>
#include <string>

namespace binary_log
{

/**
  The variable part of the Rotate event contains the name of the next binary
  log file,  and the position of the first event in the next binary log file.
*/
Rotate_event::Rotate_event(const char* buf, unsigned int event_len,
                           const Format_description_event *description_event)
: Binary_log_event(&buf, description_event->binlog_version,
                   description_event->server_version), new_log_ident(0),
  flags(DUP_NAME)
{
  //buf is advanced in Binary_log_event constructor to point to
  //beginning of post-header

  // This will ensure that the event_len is what we have at EVENT_LEN_OFFSET
  size_t header_size= description_event->common_header_len;
  uint8_t post_header_len= description_event->post_header_len[ROTATE_EVENT - 1];
  unsigned int ident_offset;

  if (event_len < header_size)
    return;


  /**
    By default, an event start immediately after the magic bytes in the binary
    log, which is at offset 4. In case if the slave has to rotate to a
    different event instead of the first one, the binary log offset for that
    event is specified in the post header. Else, the position is set to 4.
  */
  if (post_header_len)
  {
    memcpy(&pos, buf + R_POS_OFFSET, 8);
    pos= le64toh(pos);
  }
  else
    pos= 4;

  ident_len= event_len - (header_size + post_header_len);
  ident_offset= post_header_len;
  if (ident_len > FN_REFLEN - 1)
    ident_len= FN_REFLEN - 1;

  new_log_ident= bapi_strndup(buf + ident_offset, ident_len);
}


/**
   Empty ctor of Start_event_v3 called when we call the
   ctor of FDE which takes binlog_version as the parameter
   It will initialize the server_version by global variable
   server_version
*/
Start_event_v3::Start_event_v3(Log_event_type type_code_arg)
: Binary_log_event(type_code_arg),
  created(0), binlog_version(BINLOG_VERSION),
  dont_set_created(0)
{
}

/**
  Format_description_log_event 1st constructor.
*/
Format_description_event::Format_description_event(uint8_t binlog_ver,
                                                   const char* server_ver)
: Start_event_v3(FORMAT_DESCRIPTION_EVENT), event_type_permutation(0)
{
  binlog_version= binlog_ver;
  switch (binlog_ver) {
  case 4: /* MySQL 5.0 and above*/
  {
    /*
     As we are copying from a char * it might be the case at times that some
     part of the array server_version remains uninitialized so memset will help
     in getting rid of the valgrind errors.
    */
    memset(server_version, 0, ST_SERVER_VER_LEN);
    strncpy(server_version, server_ver, ST_SERVER_VER_LEN);
    if (binary_log_debug::debug_pretend_version_50034_in_binlog)
      strcpy(server_version, "5.0.34");
    common_header_len= LOG_EVENT_HEADER_LEN;
    number_of_event_types= LOG_EVENT_TYPES;
    /**
      This will be used to initialze the post_header_len,
      for binlog version 4.
    */
    static uint8_t server_event_header_length[]=
    {
      START_V3_HEADER_LEN,
      QUERY_HEADER_LEN,
      STOP_HEADER_LEN,
      ROTATE_HEADER_LEN,
      INTVAR_HEADER_LEN,
      LOAD_HEADER_LEN,
      /*
        Unused because the code for Slave log event was removed.
        (15th Oct. 2010)
      */
      0,
      CREATE_FILE_HEADER_LEN,
      APPEND_BLOCK_HEADER_LEN,
      EXEC_LOAD_HEADER_LEN,
      DELETE_FILE_HEADER_LEN,
      NEW_LOAD_HEADER_LEN,
      RAND_HEADER_LEN,
      USER_VAR_HEADER_LEN,
      FORMAT_DESCRIPTION_HEADER_LEN,
      XID_HEADER_LEN,
      BEGIN_LOAD_QUERY_HEADER_LEN,
      EXECUTE_LOAD_QUERY_HEADER_LEN,
      TABLE_MAP_HEADER_LEN,
      /*
       The PRE_GA events are never be written to any binlog, but
       their lengths are included in Format_description_log_event.
       Hence, we need to be assign some value here, to avoid reading
       uninitialized memory when the array is written to disk.
      */
      0,                                       /* PRE_GA_WRITE_ROWS_EVENT */
      0,                                       /* PRE_GA_UPDATE_ROWS_EVENT*/
      0,                                       /* PRE_GA_DELETE_ROWS_EVENT*/
      ROWS_HEADER_LEN_V1,                      /* WRITE_ROWS_EVENT_V1*/
      ROWS_HEADER_LEN_V1,                      /* UPDATE_ROWS_EVENT_V1*/
      ROWS_HEADER_LEN_V1,                      /* DELETE_ROWS_EVENT_V1*/
      INCIDENT_HEADER_LEN,
      0,                                       /* HEARTBEAT_LOG_EVENT*/
      IGNORABLE_HEADER_LEN,
      IGNORABLE_HEADER_LEN,
      ROWS_HEADER_LEN_V2,
      ROWS_HEADER_LEN_V2,
      ROWS_HEADER_LEN_V2,
       Gtid_event::POST_HEADER_LENGTH,         /*GTID_EVENT*/
       Gtid_event::POST_HEADER_LENGTH,         /*ANONYMOUS_GTID_EVENT*/
       IGNORABLE_HEADER_LEN
    };
     /*
       Allows us to sanity-check that all events initialized their
       events (see the end of this 'if' block).
    */
    post_header_len.resize(number_of_event_types +
                            BINLOG_CHECKSUM_ALG_DESC_LEN, 255);
    post_header_len.insert(post_header_len.begin(), server_event_header_length,
                            server_event_header_length + number_of_event_types);
    // Sanity-check that all post header lengths are initialized.
#ifndef DBUG_OFF
    for (int i= 0; i < number_of_event_types; i++)
      BAPI_ASSERT(post_header_len[i] != 255);
#endif
    break;
  }
  case 1: /* 3.23 */
  case 3: /* 4.0.x x >= 2 */
  {
    /*
      We build an artificial (i.e. not sent by the master) event, which
      describes what those old master versions send.
    */
    if (binlog_version == 1)
      strcpy(server_version, server_ver ? server_ver : "3.23");
    else
      strcpy(server_version, server_ver ? server_ver : "4.0");
    common_header_len= binlog_ver == 1 ? OLD_HEADER_LEN :
      LOG_EVENT_MINIMAL_HEADER_LEN;
    /*
      The first new event in binlog version 4 is Format_desc. So any event type
      after that does not exist in older versions. We use the events known by
      version 3, even if version 1 had only a subset of them (this is not a
      problem: it uses a few bytes for nothing but unifies code; it does not
      make the slave detect less corruptions).
    */
    number_of_event_types= FORMAT_DESCRIPTION_EVENT - 1;
     /**
      This will be used to initialze the post_header_len, for binlog version
      1 and 3
     */
    static uint8_t server_event_header_length_ver_1_3[]=
    {
      START_V3_HEADER_LEN,
      QUERY_HEADER_MINIMAL_LEN,
      STOP_HEADER_LEN,
      uint8_t(binlog_ver == 1 ? 0 : ROTATE_HEADER_LEN),
      INTVAR_HEADER_LEN,
      LOAD_HEADER_LEN,
      /*
       Unused because the code for Slave log event was removed.
       (15th Oct. 2010)
      */
      0,
      CREATE_FILE_HEADER_LEN,
      APPEND_BLOCK_HEADER_LEN,
      EXEC_LOAD_HEADER_LEN,
      DELETE_FILE_HEADER_LEN,
      NEW_LOAD_HEADER_LEN,
      RAND_HEADER_LEN,
      USER_VAR_HEADER_LEN
    };
    post_header_len.resize(number_of_event_types + BINLOG_CHECKSUM_ALG_DESC_LEN);
    post_header_len.insert(post_header_len.begin(),
                           server_event_header_length_ver_1_3,
                           server_event_header_length_ver_1_3 +
                           number_of_event_types);

    break;
  }
  default: /* Includes binlog version 2 i.e. 4.0.x x<=1 */
    /*
      Will make the mysql-server variable *is_valid* defined in class Log_event
      to be set to false.
    */
    break;
  }
  calc_server_version_split();
}

/**
   This method populates the array server_version_split which is then
   used for lookups to find if the server which
   created this event has some known bug.
*/
void Format_description_event::calc_server_version_split()
{
  do_server_version_split(server_version, server_version_split);
}

/**
   This method is used to find out the version of server that originated
   the current FD instance.
   @return the version of server
*/
unsigned long Format_description_event::get_product_version() const
{
  return version_product(server_version_split);
}

/**
   This method checks the MySQL version to determine whether checksums may be
   present in the events contained in the bainry log.

   @retval true  if the event's version is earlier than one that introduced
                 the replication event checksum.
   @retval false otherwise.
*/
bool Format_description_event::is_version_before_checksum() const
{
  return get_product_version() < checksum_version_product;
}


Start_event_v3::Start_event_v3(const char* buf, unsigned int event_len,
                               const Format_description_event *description_event)
  :Binary_log_event(&buf, description_event->binlog_version,
   description_event->server_version), binlog_version(BINLOG_VERSION)
{
  if (event_len < (unsigned int)description_event->common_header_len +
      ST_COMMON_HEADER_LEN_OFFSET)
  {
    server_version[0]= 0;
    return;
  }
  //buf is advanced in Binary_log_event constructor to point to
  //beginning of post-header
  memcpy(&binlog_version, buf + ST_BINLOG_VER_OFFSET, 2);
  binlog_version= le16toh(binlog_version);
  memcpy(server_version, buf + ST_SERVER_VER_OFFSET,
        ST_SERVER_VER_LEN);
  // prevent overrun if log is corrupted on disk
  server_version[ST_SERVER_VER_LEN - 1]= 0;
  created= 0;
  memcpy(&created, buf + ST_CREATED_OFFSET, 4);
  created= le64toh(created);
  dont_set_created= 1;
}

/**
  The problem with this constructor is that the fixed header may have a
  length different from this version, but we don't know this length as we
  have not read the Format_description_log_event which says it, yet. This
  length is in the post-header of the event, but we don't know where the
  post-header starts.

  So this type of event HAS to:
  - either have the header's length at the beginning (in the header, at a
  fixed position which will never be changed), not in the post-header. That
  would make the header be "shifted" compared to other events.
  - or have a header of size LOG_EVENT_MINIMAL_HEADER_LEN (19), in all future
  versions, so that we know for sure.

  I (Guilhem) chose the 2nd solution. Rotate has the same constraint (because
  it is sent before Format_description_log_event).

*/
Format_description_event::
Format_description_event(const char* buf, unsigned int event_len,
                         const Format_description_event* description_event)
 : Start_event_v3(buf, event_len, description_event), common_header_len(0),
   event_type_permutation(0)
{
  /*
   This is equivalent to (!Start_log_event_v3::is_valid()) in server,
   we dont have access to is_valid method here as it is defined in Log_event class
   in mysql-server code.
  */
  if (!(server_version[0] != 0))
    return; /* Sanity Check*/
  unsigned long ver_calc;
  buf+= LOG_EVENT_MINIMAL_HEADER_LEN;
  if ((common_header_len= buf[ST_COMMON_HEADER_LEN_OFFSET]) < OLD_HEADER_LEN)
  {
    post_header_len.clear();
    return; /* sanity check */
  }
  number_of_event_types=
   event_len - (LOG_EVENT_MINIMAL_HEADER_LEN + ST_COMMON_HEADER_LEN_OFFSET + 1);

  post_header_len.resize(number_of_event_types);
  post_header_len.insert(post_header_len.begin(),
                         buf + ST_COMMON_HEADER_LEN_OFFSET + 1,
                         (buf + ST_COMMON_HEADER_LEN_OFFSET + 1 +
                          number_of_event_types));

  calc_server_version_split();
  if ((ver_calc= get_product_version()) >= checksum_version_product)
  {
    /* the last bytes are the checksum alg desc and value (or value's room) */
    number_of_event_types -= BINLOG_CHECKSUM_ALG_DESC_LEN;
    /*
      FD from the checksum-home version server (ver_calc ==
      checksum_version_product) must have
      number_of_event_types == LOG_EVENT_TYPES.
    */
    BAPI_ASSERT(ver_calc != checksum_version_product ||
                number_of_event_types == LOG_EVENT_TYPES);
    footer()->checksum_alg= (enum_binlog_checksum_alg)
                                  post_header_len[number_of_event_types];
  }
  else
  {
    footer()->checksum_alg=  BINLOG_CHECKSUM_ALG_UNDEF;
  }

  /*
    In some previous versions, the events were given other event type
    id numbers than in the present version. When replicating from such
    a version, we therefore set up an array that maps those id numbers
    to the id numbers of the present server.
    If post_header_len is null, it means malloc failed, and in the mysql-server
    code the variable *is_valid* will be set to false, so there is no need to do
    anything.

    The trees in which events have wrong id's are:

    mysql-5.1-wl1012.old mysql-5.1-wl2325-5.0-drop6p13-alpha
    mysql-5.1-wl2325-5.0-drop6 mysql-5.1-wl2325-5.0
    mysql-5.1-wl2325-no-dd

    (this was found by grepping for two lines in sequence where the
    first matches "FORMAT_DESCRIPTION_EVENT," and the second matches
    "TABLE_MAP_EVENT," in log_event.h in all trees)

    In these trees, the following server_versions existed since
    TABLE_MAP_EVENT was introduced:
     5.1.1-a_drop5p3   5.1.1-a_drop5p4        5.1.1-alpha
    5.1.2-a_drop5p10  5.1.2-a_drop5p11       5.1.2-a_drop5p12
    5.1.2-a_drop5p13  5.1.2-a_drop5p14       5.1.2-a_drop5p15
    5.1.2-a_drop5p16  5.1.2-a_drop5p16b      5.1.2-a_drop5p16c
    5.1.2-a_drop5p17  5.1.2-a_drop5p4        5.1.2-a_drop5p5
    5.1.2-a_drop5p6   5.1.2-a_drop5p7        5.1.2-a_drop5p8
    5.1.2-a_drop5p9   5.1.3-a_drop5p17       5.1.3-a_drop5p17b
5.1.3-a_drop5p17c 5.1.4-a_drop5p18       5.1.4-a_drop5p19
    5.1.4-a_drop5p20  5.1.4-a_drop6p0        5.1.4-a_drop6p1
    5.1.4-a_drop6p2   5.1.5-a_drop5p20       5.2.0-a_drop6p3
    5.2.0-a_drop6p4   5.2.0-a_drop6p5        5.2.0-a_drop6p6
    5.2.1-a_drop6p10  5.2.1-a_drop6p11       5.2.1-a_drop6p12
    5.2.1-a_drop6p6   5.2.1-a_drop6p7        5.2.1-a_drop6p8
    5.2.2-a_drop6p13  5.2.2-a_drop6p13-alpha 5.2.2-a_drop6p13b
    5.2.2-a_drop6p13c

    (this was found by grepping for "mysql," in all historical
    versions of configure.in in the trees listed above).

    There are 5.1.1-alpha versions that use the new event id's, so we
    do not test that version string.  So replication from 5.1.1-alpha
    with the other event id's to a new version does not work.
    Moreover, we can safely ignore the part after drop[56].  This
    allows us to simplify the big list above to the following regexes:

    5\.1\.[1-5]-a_drop5.*
    5\.1\.4-a_drop6.*
    5\.2\.[0-2]-a_drop6.*

    This is what we test for in the 'if' below.
  */
  if (!post_header_len.empty() &&
      server_version[0] == '5' && server_version[1] == '.' &&
      server_version[3] == '.' &&
      strncmp(server_version + 5, "-a_drop", 7) == 0 &&
      ((server_version[2] == '1' &&
        server_version[4] >= '1' && server_version[4] <= '5' &&
        server_version[12] == '5') ||
       (server_version[2] == '1' &&
        server_version[4] == '4' &&
        server_version[12] == '6') ||
       (server_version[2] == '2' &&
        server_version[4] >= '0' && server_version[4] <= '2' &&
        server_version[12] == '6')))
  {
    // 22= No of events used in the mysql version mentioned above in the comments
    if (number_of_event_types != 22)
    {
      post_header_len.clear();
      return;
    }
    static const uint8_t perm[EVENT_TYPE_PERMUTATION_NUM]=
      {
        UNKNOWN_EVENT, START_EVENT_V3, QUERY_EVENT, STOP_EVENT, ROTATE_EVENT,
        INTVAR_EVENT, LOAD_EVENT, SLAVE_EVENT, CREATE_FILE_EVENT,
        APPEND_BLOCK_EVENT, EXEC_LOAD_EVENT, DELETE_FILE_EVENT,
        NEW_LOAD_EVENT,
        RAND_EVENT, USER_VAR_EVENT,
        FORMAT_DESCRIPTION_EVENT,
        TABLE_MAP_EVENT,
        PRE_GA_WRITE_ROWS_EVENT,
        PRE_GA_UPDATE_ROWS_EVENT,
        PRE_GA_DELETE_ROWS_EVENT,
        XID_EVENT,
        BEGIN_LOAD_QUERY_EVENT,
        EXECUTE_LOAD_QUERY_EVENT,
      };
    event_type_permutation= perm;
    /*
      Since we use (permuted) event id's to index the post_header_len
      array, we need to permute the post_header_len array too.
    */
    uint8_t post_header_len_temp[EVENT_TYPE_PERMUTATION_NUM];
    for (unsigned int i= 1; i < EVENT_TYPE_PERMUTATION_NUM; i++)
      post_header_len_temp[perm[i] - 1]= post_header_len[i - 1];
    for (unsigned int i= 0; i < EVENT_TYPE_PERMUTATION_NUM - 1; i++)
      post_header_len[i] = post_header_len_temp[i];

  }
    return;
}


Format_description_event::~Format_description_event()
{
}

/**
  Constructor of Incident_event
*/
Incident_event::Incident_event(const char *buf, unsigned int event_len,
                               const Format_description_event *descr_event)
: Binary_log_event(&buf, descr_event->binlog_version,
                   descr_event->server_version)
{
  //buf is advanced in Binary_log_event constructor to point to
  //beginning of post-header
  uint8_t const common_header_len= descr_event->common_header_len;
  uint8_t const post_header_len= descr_event->post_header_len[INCIDENT_EVENT-1];

  message= NULL;
  message_length= 0;
  uint16_t incident_number;
  memcpy(&incident_number, buf, 2);
  incident_number= le16toh(incident_number);
  if (incident_number >= INCIDENT_COUNT ||
      incident_number <= INCIDENT_NONE)
  {
    /*
      If the incident is not recognized, this binlog event is
      invalid.
    */
    incident= INCIDENT_NONE;

  }

  incident= static_cast<enum_incident>(incident_number);
  char const *ptr= buf + post_header_len;
  char const *const str_end= buf - common_header_len + event_len;
  uint8_t len= 0;                   // Assignment to keep compiler happy
  const char *str= NULL;          // Assignment to keep compiler happy
  read_str_at_most_255_bytes(&ptr, str_end, &str, &len);
  if (!(message= static_cast<char*>(bapi_malloc(len + 1, 16))))
  {
    /* Mark this event invalid */
    incident= INCIDENT_NONE;
    return;
  }

  //bapi_strmake(message, str, len);
  strncpy(message, str, len);
  //Appending '\0' at the end.
  message[len]= '\0';
  message_length= len;
  return;
}

Incident_event::~Incident_event()
{
  if(message)
    bapi_free(message);
}
Xid_event::
Xid_event(const char* buf,
          const Format_description_event *description_event)
  :Binary_log_event(&buf, description_event->binlog_version,
                    description_event->server_version)
{
  //buf is advanced in Binary_log_event constructor to point to
  //beginning of post-header
  /*
   We step to the post-header despite it being empty because it could later be
   filled with something and we have to support that case.
   The Variable Data part begins immediately.
  */
  buf+= description_event->post_header_len[XID_EVENT - 1];
  memcpy((char*) &xid, buf, 8);
}

/**
    We create an object of Ignorable_log_event for unrecognized sub-class, while
    decoding. So that we just update the position and continue.

    @param buf                Contains the serialized event.
    @param length             Length of the serialized event.
    @param description_event  An FDE event, used to get the
                              following information
                              -binlog_version
                              -server_version
                              -post_header_len
                              -common_header_len
                              The content of this object
                              depends on the binlog-version currently in use.
*/
Ignorable_event::Ignorable_event(const char *buf,
                                 const Format_description_event *descr_event)
 : Binary_log_event(&buf, descr_event->binlog_version,
                    descr_event->server_version)
{}


/**
  ctor of Gtid_event
  Each transaction has a coordinate in the form of a pair:
  GTID = (SID, GNO)
  GTID stands for Global Transaction IDentifier, SID for Source Identifier,
  and GNO for Group Number.

  SID is a 128-bit number that identifies the place where the transaction was
  first committed. SID is normally the SERVER_UUID of a server, but may be
  something different if the transaction was generated by something else than
  a MySQL server).

  GNO is a 64-bit sequence number: 1 for the first transaction committed on SID,
  2 for the second transaction, and so on. No transaction can have GNO 0
*/

Gtid_event::Gtid_event(const char *buffer, uint32_t event_len,
                       const Format_description_event *description_event)
 : Binary_log_event(&buffer, description_event->binlog_version,
                    description_event->server_version),
    last_committed(SEQ_UNINIT), sequence_number(SEQ_UNINIT)
{
  //buf is advanced in Binary_log_event constructor to point to
  //beginning of post-header
  uint8_t const common_header_len= description_event->common_header_len;

  char const *ptr_buffer= buffer;

  commit_flag= *ptr_buffer != 0;
  ptr_buffer+= ENCODED_FLAG_LENGTH;

  memcpy(Uuid_parent_struct.bytes, (const unsigned char*)ptr_buffer,
          Uuid_parent_struct.BYTE_LENGTH);
  ptr_buffer+= ENCODED_SID_LENGTH;

  // SIDNO is only generated if needed, in get_sidno().
  gtid_info_struct.rpl_gtid_sidno= -1;

  memcpy(&(gtid_info_struct.rpl_gtid_gno), ptr_buffer,
         sizeof(gtid_info_struct.rpl_gtid_gno));
  gtid_info_struct.rpl_gtid_gno= le64toh(gtid_info_struct.rpl_gtid_gno);
  ptr_buffer+= ENCODED_GNO_LENGTH;

  /* fetch the commit timestamp */
  if (*ptr_buffer == G_COMMIT_TS2 &&
      (static_cast<unsigned int>(ptr_buffer - (buffer - common_header_len)) + 1 +
      COMMIT_SEQ_LEN) <= event_len)
  {
    ptr_buffer++;
    memcpy(&last_committed, ptr_buffer, sizeof(last_committed));
    last_committed= (int64_t)le64toh(last_committed);
    memcpy(&sequence_number, ptr_buffer + 8, sizeof(sequence_number));
    sequence_number= (int64_t)le64toh(sequence_number);
    ptr_buffer+= COMMIT_SEQ_LEN;
  }
  return;
}

/**
  Constructor of Previous_gtids_event
  Decodes the gtid_executed in the last binlog file
*/

Previous_gtids_event::
Previous_gtids_event(const char *buffer, unsigned int event_len,
                     const Format_description_event *description_event)
: Binary_log_event(&buffer, description_event->binlog_version,
                     description_event->server_version)
{
  //buf is advanced in Binary_log_event constructor to point to
  //beginning of post-header
  uint8_t const common_header_len= description_event->common_header_len;
  uint8_t const post_header_len=
    description_event->post_header_len[PREVIOUS_GTIDS_LOG_EVENT - 1];

  buf= (const unsigned char *)buffer  + post_header_len;
  buf_size= event_len - common_header_len - post_header_len;
  return;
}


Transaction_context_event::
Transaction_context_event(const char *buffer, unsigned int event_len,
                          const Format_description_event *description_event)
: Binary_log_event(&buffer, description_event->binlog_version,
                   description_event->server_version)
{
  const char* data_head = buffer;
  size_t server_uuid_len= (unsigned int) data_head[ENCODED_SERVER_UUID_LEN_OFFSET];
  thread_id= (uint64_t) le64toh(*(data_head + ENCODED_THREAD_ID_OFFSET));
  gtid_specified= (int) data_head[ENCODED_GTID_SPECIFIED_OFFSET];
  uint16_t write_set_len= (uint16_t) le16toh(*(data_head +
                                               ENCODED_WRITE_SET_ITEMS_OFFSET));
  uint16_t read_set_len= (uint16_t) le16toh(*(data_head +
                                              ENCODED_READ_SET_ITEMS_OFFSET));
  const char *pos = data_head + POST_HEADER_LENGTH;

  server_uuid= bapi_strndup(pos, server_uuid_len);
  if (server_uuid == NULL)
    goto err;
  pos += server_uuid_len;

  pos= (const char*) read_data_set((char*)pos, write_set_len, &write_set);
  if (pos == NULL)
    goto err;
  pos= (const char*) read_data_set((char*)pos, read_set_len, &read_set);
  if (pos == NULL)
    goto err;

   return;

err:
  // Make is_valid() return false.
  free((void*) server_uuid);
  server_uuid= NULL;
  return;
}

char* Transaction_context_event::read_data_set(char *pos,
                                               uint16_t set_len,
                                               std::list<const char*> *set)
{
  for (int i=0; i < set_len; i++)
  {
    uint16_t len= (uint16_t)(*pos);
    pos += ENCODED_READ_WRITE_SET_ITEM_LEN;
    const char *hash= bapi_strndup(pos, len);
    if (hash == NULL)
      return(NULL);
    pos += len;
    set->push_back(hash);
  }

  return(pos);
}


View_change_event::
View_change_event(const char *buffer, unsigned int event_len,
                  const Format_description_event *description_event)
: Binary_log_event(&buffer, description_event->binlog_version,
                   description_event->server_version),
  seq_number(0)
{
  const char* data_header= buffer; //+ description_event->common_header_len;
  memcpy(view_id, data_header, ENCODED_VIEW_ID_MAX_LEN);
  seq_number= (int64_t) le64toh(*(data_header + ENCODED_SEQ_NUMBER_OFFSET));
  uint32_t cert_info_len= (uint32_t) le32toh(*(data_header +
                                               ENCODED_CERT_INFO_SIZE_OFFSET));
  char *pos = (char*) data_header + POST_HEADER_LENGTH;

  pos= read_data_map(pos, cert_info_len, &certification_info);
  if (pos == NULL)
    goto err;

  return;

err:
  // Make is_valid() return false.
  view_id[0]= '\0';
  return;

}

char* View_change_event::read_data_map(char *pos,
                                       unsigned int map_len,
                                       std::map<std::string, std::string> *map)
{
  BAPI_ASSERT(map->empty());
  for (unsigned int i= 0; i < map_len; i++)
  {
    uint16_t key_len= le16toh(*pos);
    pos+= ENCODED_CERT_INFO_KEY_SIZE_LEN;

    std::string key(pos, key_len);
    pos+= key_len;

    uint16_t value_len= (uint16_t) le16toh(*pos);
    pos+= ENCODED_CERT_INFO_VALUE_LEN;

    std::string value(pos, value_len);
    pos+= value_len;

    (*map)[key]= value;
  }
  return(pos);
}

Heartbeat_event::Heartbeat_event(const char* buf, unsigned int event_len,
                                 const Format_description_event*
                                 description_event)
: Binary_log_event(&buf, description_event->binlog_version,
                   description_event->server_version),
  log_ident(buf)
{
  //buf is advanced in Binary_log_event constructor to point to
  //beginning of post-header
  unsigned char header_size= description_event->common_header_len;
  ident_len= event_len - header_size;
  if (ident_len > FN_REFLEN - 1)
    ident_len= FN_REFLEN - 1;
}

#ifndef HAVE_MYSYS
void Rotate_event::print_event_info(std::ostream& info)
{
  info << "Binlog Position: " << pos;
  info << ", Log name: " << new_log_ident;
}

void Rotate_event::print_long_info(std::ostream& info)
{
  info << "Timestamp: " << header()->when.tv_sec;
  info << "\t";
  this->print_event_info(info);
}

void Format_description_event::print_event_info(std::ostream& info)
{
  info << "Server ver: " << server_version;
  info << ", Binlog ver: " << binlog_version;
}

void Format_description_event::print_long_info(std::ostream& info)
{
  this->print_event_info(info);
  info << "\nCreated timestamp: " << created;
  info << "\tCommon Header Length: " << (int)common_header_len;
}


void Incident_event::print_event_info(std::ostream& info)
{
  info << get_message();
  info << get_incident_type();
}

void Incident_event::print_long_info(std::ostream& info)
{
  this->print_event_info(info);
}

void Xid_event::print_event_info(std::ostream& info)
{
  info << "Xid ID=" << xid;
}

void Xid_event::print_long_info(std::ostream& info)
{
  info << "Timestamp: " << header()->when.tv_sec;
  info << "\t";
  this->print_event_info(info);
}

#endif //end HAVE_MYSYS

}// end namespace binary_log


