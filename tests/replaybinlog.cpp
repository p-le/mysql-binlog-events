/*
Copyright (c) 2003, 2011, 2013, Oracle and/or its affiliates. All rights
reserved.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2 of
the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
02110-1301  USA
*/
#include <stdlib.h>
#include "binlog.h"

bool is_text_field(Value &val)
{
  if (val.type() == MYSQL_TYPE_VARCHAR ||
      val.type() == MYSQL_TYPE_BLOB ||
      val.type() == MYSQL_TYPE_MEDIUM_BLOB ||
      val.type() == MYSQL_TYPE_LONG_BLOB)
    return true;
  return false;
}

void table_insert(const std::string& table_name, binary_log::Row_of_fields &fields)
{
  std::cout << "INSERT INTO "
           << table_name
           << " VALUES (";
  binary_log::Row_of_fields::iterator field_it= fields.begin();
  binary_log::Converter converter;
  do {
    /*
     Each row contains a vector of Value objects. The converter
     allows us to transform the value into another
     representation.
    */
    std::string str;
    converter.to(str, *field_it);
    if (is_text_field(*field_it))
      std::cout << '\'';
    std::cout << str;
    if (is_text_field(*field_it))
      std::cout << '\'';
    ++field_it;
    if (field_it != fields.end())
      std::cout << ", ";
  } while(field_it != fields.end());
  std::cout << ")" << std::endl;
}


void table_update(const std::string& table_name,
                  binary_log::Row_of_fields &old_fields,
                  binary_log::Row_of_fields &new_fields)
{
  std::cout << "UPDATE "
           << table_name
           << " SET ";

  int field_id= 0;
  binary_log::Row_of_fields::iterator field_it= new_fields.begin();
  binary_log::Converter converter;
  do {
    std::string str;
    converter.to(str, *field_it);
    std::cout << field_id << "= ";
    if (is_text_field(*field_it))
      std::cout << '\'';
    std::cout << str;
    if (is_text_field(*field_it))
      std::cout << '\'';
    ++field_it;
    ++field_id;
    if (field_it != new_fields.end())
      std::cout << ", ";
  } while(field_it != new_fields.end());
  std::cout << " WHERE ";
  field_it= old_fields.begin();
  field_id= 0;
  do {
    std::string str;
    converter.to(str, *field_it);
    std::cout << field_id << "= ";
    if (is_text_field(*field_it))
      std::cout << '\'';
    std::cout << str;
    if (is_text_field(*field_it))
      std::cout << '\'';
    ++field_it;
    ++field_id;
    if (field_it != old_fields.end())
      std::cout << " AND ";
  } while(field_it != old_fields.end());
  std::cout << " LIMIT 1" << std::endl;

}


void table_delete(const std::string& table_name, binary_log::Row_of_fields &fields)
{
  std::cout << "DELETE FROM "
           << table_name
           << " WHERE ";
  binary_log::Row_of_fields::iterator field_it= fields.begin();
  int field_id= 0;
  binary_log::Converter converter;
  do {

    std::string str;
    converter.to(str, *field_it);
    std::cout << field_id << "= ";
    if (is_text_field(*field_it))
      std::cout << '\'';
    std::cout << str;
    if (is_text_field(*field_it))
      std::cout << '\'';
    ++field_it;
    ++field_id;
    if (field_it != fields.end())
      std::cout << " AND ";
  } while(field_it != fields.end());
  std::cout << " LIMIT 1" << std::endl;
}

class Incident_handler : public binary_log::Content_handler
{
public:
 Incident_handler() : binary_log::Content_handler() {}

 binary_log::Binary_log_event *process_event(binary_log::Incident_event *incident)
 {
   std::cout << "Event type: "
             << binary_log::system::get_event_type_str(incident->get_event_type())
             << " length: " << incident->header()->event_length
             << " next pos: " << incident->header()->next_position
             << std::endl;
   std::cout << "type= "
             << (unsigned)incident->type
             << " message= "
             << incident->message
             <<  std::endl
             <<  std::endl;
   /* Consume the event */
   delete incident;
   return 0;
 }
};

class Replay_binlog : public binary_log::Content_handler
{
public:
  Replay_binlog() : binary_log::Content_handler() {}
  binary_log::Binary_log_event *process_event(binary_log::Binary_log_event *event)
  {
    if (event->get_event_type() != binary_log::USER_DEFINED)
      return event;
    std::cout << "Event type: "
            << binary_log::system::get_event_type_str(event->get_event_type())
            << " length: " << event->header()->event_length
            << " next pos: " << event->header()->next_position
            << std::endl;
    binary_log::Transaction_log_event *trans=
    static_cast<binary_log::Transaction_log_event *>(event);
    int row_count=0;
    /*
      The transaction event we created has aggregated all row events in an
      ordered list.
    */
    std::list<Binary_log_event *>::iterator it= (trans->m_events).begin();
    binary_log::Binary_log_event * event1;
    for (; it != (trans->m_events).end(); it++)
    {
      event1= *it;
      switch (event->get_event_type())
      {
        case binary_log::WRITE_ROWS_EVENT:
        case binary_log::WRITE_ROWS_EVENT_V1:
        case binary_log::DELETE_ROWS_EVENT:
        case binary_log::DELETE_ROWS_EVENT_V1:
        case binary_log::UPDATE_ROWS_EVENT:
        case binary_log::UPDATE_ROWS_EVENT_V1:
        binary_log::Row_event *rev= static_cast<binary_log::Row_event *>(event1);
        uint64_t table_id= rev->table_id;
        // BUG: will create a new event header if the table id doesn't exist.
        Binary_log_event * tmevent= trans->table_map()[table_id];
        binary_log::Table_map_event *tm;
        if (tmevent != NULL)
          tm= static_cast<binary_log::Table_map_event *>(tmevent);
        else
        {
          std::cout << "Table id "
                    << table_id
                    << " was not registered by any preceding table map event."
                    << std::endl;
          continue;
        }
        /*
         Each row event contains multiple rows and fields. The Row_iterator
         allows us to iterate one row at a time.
        */
        binary_log::Row_event_set rows(rev, tm);
        /*
         Create a fuly qualified table name
        */
        std::ostringstream os;
        os << tm->db_name << '.' << tm->table_name;
        try
        {
          binary_log::Row_event_set::iterator it= rows.begin();
          do {
            binary_log::Row_of_fields fields= *it;

            if (event->get_event_type() == binary_log::WRITE_ROWS_EVENT ||
                event->get_event_type() == binary_log::WRITE_ROWS_EVENT_V1)
                   table_insert(os.str(),fields);
            if (event->get_event_type() == binary_log::UPDATE_ROWS_EVENT ||
                event->get_event_type() == binary_log::UPDATE_ROWS_EVENT_V1)
            {
              ++it;
              binary_log::Row_of_fields fields2= *it;
              table_update(os.str(),fields,fields2);
            }
            if (event->get_event_type() == binary_log::DELETE_ROWS_EVENT ||
                event->get_event_type() == binary_log::DELETE_ROWS_EVENT_V1)
              table_delete(os.str(),fields);
          } while (++it != rows.end());
        }
        catch (const std::logic_error& le)
        {
          std::cerr << "MySQL Data Type error: " << le.what() << '\n';
        }

      } // end switch
    } // end for loop
    /* Consume the event */
    delete trans;
    return 0;
  }
};
/*
 *
 */
int main(int argc, char** argv)
{
  if (argc != 2)
  {
    fprintf(stderr,"Usage:\n\treplaybinlog URL\n\nExample:"
            "\n\treplaybinlog mysql://root:mypasswd@127.0.0.1:3306\n\n");
    return (EXIT_FAILURE);
  }

  binary_log::Binary_log binlog(binary_log::system::create_transport(argv[1]));


  /*
    Attach a custom event parser which produces user defined events
  */
  binary_log::Basic_transaction_parser transaction_parser;
  Incident_handler incident_hndlr;
  Replay_binlog replay_hndlr;
  Decoder d;

  binlog.content_handler_pipeline()->push_back(&transaction_parser);
  binlog.content_handler_pipeline()->push_back(&incident_hndlr);
  binlog.content_handler_pipeline()->push_back(&replay_hndlr);

  if (binlog.connect())
  {
    fprintf(stderr,"Can't connect to the master.\n");
    return (EXIT_FAILURE);
  }

  if (binlog.set_position(4) != ERR_OK)
  {
    fprintf(stderr,"Can't reposition the binary log reader.\n");
    return (EXIT_FAILURE);
  }

  Binary_log_event  *event;

  bool quit= false;
  while(!quit)
  {
    /*
     Pull events from the master. This is the heart beat of the event listener.
    */
    int error_number;
    std::pair<unsigned char *, size_t> buffer_buflen;
    error_number= drv->get_next_event(&buffer_buflen);

    if (error_number == ERR_OK)
    {
      const char *error;
      event= decode.decode_event((char*)buffer_buflen.first, buffer_buflen.second,
                                 &error, 1);
    }
    if (error_number != 0)
    {
      quit= true;
      continue;
    }

    /*
     Print the event
    */
    std::cout << "Event type: "
              << binary_log::system::get_event_type_str(event->get_event_type())
              << " length: " << event->header()->event_length
              << " next pos: " << event->header()->next_position
              << std::endl;

    /*
     Perform a special action based on event type
    */

    switch(event->header()->type_code)
    {
    case binary_log::QUERY_EVENT:
      {
        const binary_log::Query_event *qev=
        static_cast<const binary_log::Query_event *>(event);
        std::cout << "query= "
                  << qev->query
                  << " db= "
                  << qev->db_name
                  <<  std::endl
                  <<  std::endl;
        if (qev->query.find("DROP TABLE REPLICATION_LISTENER") !=
            std::string::npos ||
            qev->query.find("DROP TABLE `REPLICATION_LISTENER`") !=
            std::string::npos)
        {
          quit= true;
        }
      }
      break;

    case binary_log::ROTATE_EVENT:
      {
        binary_log::Rotate_event *rot= static_cast<binary_log::Rotate_event *>(event);
        std::cout << "filename= "
                  << rot->binlog_file.c_str()
                  << " pos= "
                  << rot->binlog_pos
                  << std::endl
                  << std::endl;
      }
      break;

    } // end switch
    delete event;
  } // end loop
  return (EXIT_SUCCESS);
}
