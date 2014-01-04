#include "emacs.hh"
#include "util.hh"
#include "NetworkConnection.hh"
#include "UserFunction.hh"

#include <iostream>
#include <sstream>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

std::string NetworkConnection::read_line_from_fd()
{
    std::stringstream in;
    int end = 0;
    while( !end ) {
        char buf[1024];

        int res = read( socket_fd, (void *)buf, sizeof( buf ) - 1 );
        if( res == -1 ) {
            throw ConnectionError( "network error" );
        }
        if( res == 0 ) {
            throw ConnectionError( "disconnected" );
        }

        if( buf[res - 1] == '\n' ) {
            if( res >= 2 && buf[res - 2] == '\r' ) {
                buf[res - 2] = 0;
            }
            else {
                buf[res - 1] = 0;
            }
            end = 1;
        }
        else {
            buf[res] = 0;
        }
        in << buf;
    }
    return in.str();
}

void NetworkConnection::write_string_to_fd( const std::string &s )
{
    const char *buf = s.c_str();
    int n = strlen( buf );
    int pos = 0;
    while( pos < n ) {
        int res = write( socket_fd, buf + pos, n - pos );
        if( res == -1 ) {
            abort();
        }
        pos += res;
    }
}

std::vector<std::string> NetworkConnection::load_block( void )
{
    std::vector<std::string> result;
    while( 1 ) {
        COUT << "reading line" << endl;
        std::string v = read_line_from_fd();
        COUT << "got line:'" << v << "', comp:" << v.compare(END_TAG) << endl;
        if( v == END_TAG ) {
            break;
        }
        result.push_back( v );
    }
    return result;
}

void NetworkConnection::show_function( const std::string &name )
{
    std::stringstream out;

    UCS_string ucs_name( name.c_str() );
    NamedObject *obj = Workspace::lookup_existing_name( ucs_name );
    if( obj == NULL ) {
        out << "undefined\n";
    }
    else if( !obj->is_user_defined() ) {
        out << "system function\n";
    }
    else {
        const Function *function = obj->get_function();
        if( function == NULL ) {
            out << "symbol is not a function";
        }
        else if( function->get_exec_properties()[0] != 0 ) {
            out << "function is not executable\n";
        }
        else {
            const UCS_string ucs = function->canonical( false );
            vector<UCS_string> tlines;
            ucs.to_vector( tlines );

            for( vector<UCS_string>::iterator i = tlines.begin() ; i != tlines.end() ; i++ ) {
                out << i->to_string() << "\n";
            }
        }
    }
    out << END_TAG << "\n";

    write_string_to_fd( out.str() );
}

void NetworkConnection::clear_si_stack( void )
{
    Workspace::clear_SI( COUT );
}

void NetworkConnection::send_function( const std::vector<std::string> &content )
{
    UCS_string fun_text;
    for( vector<string>::const_iterator i = content.begin() ; i != content.end() ; i++ ) {
        fun_text.append( UCS_string( i->c_str() ) );
        fun_text.append( UNI_ASCII_LF );
    }
    int line = 0;
    UserFunction *ufun = UserFunction::fix( fun_text, line, false, LOC );
    if( ufun == NULL ) {
        write_string_to_fd( "error" );
    }
    else {
        write_string_to_fd( "function_defined" );
    }
    write_string_to_fd( "\n" END_TAG "\n" );
}

int NetworkConnection::process_command( const std::string &command )
{
    LockWrapper lock;
    std::vector<std::string> elements = split( command, ':' );
    if( elements.size() > 0 ) {
        std::string operation = elements[0];
        if( operation == "si" ) {
            show_si();
        }
        else if( operation == "sic" ) {
            clear_si_stack();
        }
        else if( operation == "fn" ) {
            show_function( elements[1] );
        }
        else if( operation == "def" ) {
            vector<string> content = load_block();
            send_function( content );
        }
        else if( operation == "quit" ) {
            close( socket_fd );
            throw ConnectionError( "quit received" );
        }
        else {
            CERR << "unknown command: '" << operation << "'" << endl;
        }
    }
    else {
        CERR << "empty command" << endl;
    }
    return 0;
}

void NetworkConnection::show_si( void )
{
    std::stringstream out;
    for( const StateIndicator *si = Workspace::SI_top() ; si ; si = si->get_parent() ) {
        out << si->function_name() << "\n";
    }
    out << END_TAG << "\n";

    write_string_to_fd( out.str() );
}

void NetworkConnection::run( void )
{
    int end = 0;
    while( !end ) {
        std::string command = read_line_from_fd();
        end = process_command( command );
    }
}
