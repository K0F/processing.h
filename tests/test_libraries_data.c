/* Unit tests for processing.h native libraries: Data, Network & IO
 * Tests BufferedReader, FileReader, PrintWriter, XMLElement, Client & Server.
 */

#include "libraries/io.h"
#include "libraries/xml.h"
#include "libraries/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, ...) do { \
  checks++; \
  if (!(cond)) { \
    failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
  } \
} while (0)

static void test_buffered_reader_file(void) {
  const char *tmp_path = "/tmp/test_pde_io_data.txt";
  FILE *fp = fopen(tmp_path, "w");
  CHECK(fp != NULL, "Failed to open temporary file %s for writing", tmp_path);
  if (!fp) return;
  fprintf(fp, "Line 1: Hello Processing\n");
  fprintf(fp, "Line 2: Native Libraries\n");
  fprintf(fp, "Line 3: C99 Architecture\n");
  fclose(fp);

  /* 1. Test createReader */
  BufferedReader r1 = createReader(tmp_path);
  CHECK(r1.fp != NULL, "createReader failed to open file");
  const char *l1 = BufferedReader_readLine(&r1);
  CHECK(l1 != NULL && strcmp(l1, "Line 1: Hello Processing") == 0,
        "Line 1 mismatch: got '%s'", l1 ? l1 : "(null)");
  const char *l2 = BufferedReader_readLine(&r1);
  CHECK(l2 != NULL && strcmp(l2, "Line 2: Native Libraries") == 0,
        "Line 2 mismatch: got '%s'", l2 ? l2 : "(null)");
  const char *l3 = BufferedReader_readLine(&r1);
  CHECK(l3 != NULL && strcmp(l3, "Line 3: C99 Architecture") == 0,
        "Line 3 mismatch: got '%s'", l3 ? l3 : "(null)");
  const char *l4 = BufferedReader_readLine(&r1);
  CHECK(l4 == NULL, "Expected EOF (NULL), got '%s'", l4);
  BufferedReader_close(&r1);
  CHECK(r1.fp == NULL, "BufferedReader_close did not set fp to NULL");

  /* 2. Test new BufferedReader(new FileReader(path)) expansion */
  BufferedReader r2 = BufferedReader_ctor(FileReader_ctor(tmp_path));
  CHECK(r2.fp != NULL, "BufferedReader_ctor(FileReader_ctor(...)) failed to open file");
  const char *r2_l1 = BufferedReader_readLine(&r2);
  CHECK(r2_l1 != NULL && strcmp(r2_l1, "Line 1: Hello Processing") == 0,
        "FileReader Line 1 mismatch: got '%s'", r2_l1 ? r2_l1 : "(null)");
  BufferedReader_close(&r2);

  /* 3. Test BufferedReader_ctor directly with string path */
  BufferedReader r3 = BufferedReader_ctor(tmp_path);
  CHECK(r3.fp != NULL, "BufferedReader_ctor(path) failed to open file");
  const char *r3_l1 = BufferedReader_readLine(&r3);
  CHECK(r3_l1 != NULL && strcmp(r3_l1, "Line 1: Hello Processing") == 0,
        "Path Line 1 mismatch: got '%s'", r3_l1 ? r3_l1 : "(null)");
  BufferedReader_close(&r3);

  /* 4. Test NULL safety */
  BufferedReader r_null = createReader(NULL);
  CHECK(BufferedReader_readLine(&r_null) == NULL, "createReader(NULL) readLine should be NULL");
  CHECK(BufferedReader_readLine(NULL) == NULL, "BufferedReader_readLine(NULL) should be NULL");
  BufferedReader_close(&r_null);
  BufferedReader_close(NULL);

  unlink(tmp_path);
}

static void test_print_writer(void) {
  const char *tmp_path = "/tmp/test_pde_pw_data.txt";
  PrintWriter w = createWriter(tmp_path);
  CHECK(w.fp != NULL, "createWriter failed to open file");
  PrintWriter_println(&w, "First line");
  PrintWriter_print(&w, "Second ");
  PrintWriter_println(&w, "line");
  PrintWriter_flush(&w);
  PrintWriter_close(&w);
  CHECK(w.fp == NULL, "PrintWriter_close did not set fp to NULL");

  /* Read back with BufferedReader */
  BufferedReader r = createReader(tmp_path);
  const char *l1 = BufferedReader_readLine(&r);
  CHECK(l1 != NULL && strcmp(l1, "First line") == 0, "PW line 1 mismatch: '%s'", l1 ? l1 : "(null)");
  const char *l2 = BufferedReader_readLine(&r);
  CHECK(l2 != NULL && strcmp(l2, "Second line") == 0, "PW line 2 mismatch: '%s'", l2 ? l2 : "(null)");
  BufferedReader_close(&r);

  /* NULL safety */
  PrintWriter_println(NULL, "test");
  PrintWriter_print(NULL, "test");
  PrintWriter_flush(NULL);
  PrintWriter_close(NULL);

  unlink(tmp_path);
}

static void test_xml_element(void) {
  const char *xml_str = "<weather temp=\"21.5\" condition=\"sunny\"><city name=\"Prague\"/></weather>";

  /* Parse inline XML string */
  XMLElement xml = XMLElement_ctor(NULL, xml_str);
  CHECK(xml != NULL, "XMLElement_ctor returned NULL for valid XML string");
  if (!xml) return;

  CHECK(strcmp(XMLElement_getName(xml), "weather") == 0,
        "Root element name want 'weather', got '%s'", XMLElement_getName(xml));

  float temp = XMLElement_getFloatAttribute(xml, "temp");
  CHECK(fabsf(temp - 21.5f) < 0.001f, "temp want 21.5, got %f", temp);

  const char *condition = XMLElement_getStringAttribute(xml, "condition");
  CHECK(condition != NULL && strcmp(condition, "sunny") == 0,
        "condition want 'sunny', got '%s'", condition ? condition : "(null)");

  CHECK(XMLElement_getChildCount(xml) == 1,
        "getChildCount want 1, got %d", XMLElement_getChildCount(xml));

  /* Test getChild by name */
  XMLElement city = XMLElement_getChild(xml, "city");
  CHECK(city != NULL, "XMLElement_getChild(xml, 'city') returned NULL");
  if (city) {
    CHECK(strcmp(XMLElement_getName(city), "city") == 0,
          "city name want 'city', got '%s'", XMLElement_getName(city));
    const char *city_name = XMLElement_getStringAttribute(city, "name");
    CHECK(city_name != NULL && strcmp(city_name, "Prague") == 0,
          "city attribute 'name' want 'Prague', got '%s'", city_name ? city_name : "(null)");
  }

  /* Test getChild_idx */
  XMLElement city0 = XMLElement_getChild_idx(xml, 0);
  CHECK(city0 == city, "XMLElement_getChild_idx(xml, 0) returned different pointer than getChild");

  /* Test _Generic getChild with index */
  XMLElement city_gen0 = XMLElement_getChild(xml, 0);
  CHECK(city_gen0 == city, "XMLElement_getChild(xml, 0) via _Generic did not match getChild_idx");

  /* Test missing child / attribute */
  CHECK(XMLElement_getChild(xml, "nonexistent") == NULL, "Missing child should be NULL");
  CHECK(XMLElement_getChild_idx(xml, 99) == NULL, "Out of bounds child index should be NULL");
  CHECK(XMLElement_getStringAttribute(xml, "missing") == NULL, "Missing attribute should be NULL");
  CHECK(XMLElement_getIntAttribute(xml, "missing") == 0, "Missing int attribute should be 0");
  CHECK(XMLElement_getFloatAttribute(xml, "missing") == 0.0f, "Missing float attribute should be 0.0f");

  XMLElement_free(xml);

  /* Test XML with text content and CDATA */
  const char *xml_content_str =
    "<note id=\"42\">\n"
    "  <to>Tove</to>\n"
    "  <from>Jani</from>\n"
    "  <body>Don't forget the party!</body>\n"
    "</note>";

  XMLElement note = XMLElement_ctor(xml_content_str);
  CHECK(note != NULL, "Failed to parse note XML");
  if (note) {
    CHECK(XMLElement_getIntAttribute(note, "id") == 42,
          "note id want 42, got %d", XMLElement_getIntAttribute(note, "id"));
    CHECK(XMLElement_getChildCount(note) == 3,
          "note childCount want 3, got %d", XMLElement_getChildCount(note));

    XMLElement to = XMLElement_getChild(note, "to");
    CHECK(to != NULL && strcmp(XMLElement_getContent(to), "Tove") == 0,
          "to content want 'Tove', got '%s'", to ? XMLElement_getContent(to) : "(null)");

    XMLElement from = XMLElement_getChild(note, "from");
    CHECK(from != NULL && strcmp(XMLElement_getContent(from), "Jani") == 0,
          "from content want 'Jani', got '%s'", from ? XMLElement_getContent(from) : "(null)");

    XMLElement body = XMLElement_getChild(note, "body");
    CHECK(body != NULL && strcmp(XMLElement_getContent(body), "Don't forget the party!") == 0,
          "body content want 'Don't forget the party!', got '%s'",
          body ? XMLElement_getContent(body) : "(null)");

    XMLElement_free(note);
  }

  /* Test parsing from a file path */
  const char *tmp_xml = "/tmp/test_weather.xml";
  FILE *xfp = fopen(tmp_xml, "w");
  CHECK(xfp != NULL, "Failed to create temp XML file");
  if (xfp) {
    fputs(xml_str, xfp);
    fclose(xfp);

    XMLElement xml_file = XMLElement_ctor(tmp_xml);
    CHECK(xml_file != NULL, "XMLElement_ctor(path) failed");
    if (xml_file) {
      CHECK(strcmp(XMLElement_getName(xml_file), "weather") == 0,
            "File XML root name want 'weather', got '%s'", XMLElement_getName(xml_file));
      XMLElement fcity = XMLElement_getChild(xml_file, "city");
      CHECK(fcity != NULL && strcmp(XMLElement_getStringAttribute(fcity, "name"), "Prague") == 0,
            "File XML city attribute name want 'Prague'");
      XMLElement_free(xml_file);
    }
    unlink(tmp_xml);
  }

  /* NULL safety */
  CHECK(strcmp(XMLElement_getName(NULL), "") == 0, "getName(NULL) should be empty string");
  CHECK(strcmp(XMLElement_getContent(NULL), "") == 0, "getContent(NULL) should be empty string");
  CHECK(XMLElement_getChildCount(NULL) == 0, "getChildCount(NULL) should be 0");
  CHECK(XMLElement_getChild_idx(NULL, 0) == NULL, "getChild_idx(NULL) should be NULL");
  CHECK(XMLElement_getStringAttribute(NULL, "a") == NULL, "getStringAttribute(NULL) should be NULL");
  CHECK(XMLElement_getIntAttribute(NULL, "a") == 0, "getIntAttribute(NULL) should be 0");
  CHECK(XMLElement_getFloatAttribute(NULL, "a") == 0.0f, "getFloatAttribute(NULL) should be 0.0f");
  XMLElement_free(NULL);
}

static void test_net_server_client(void) {
  int port = 26543;
  Server s = Server_ctor(port);
  CHECK(s.sock >= 0, "Server_ctor failed to bind/listen on port %d", port);
  if (s.sock < 0) return;

  Client c = Client_ctor("127.0.0.1", port);
  CHECK(c.sock >= 0, "Client_ctor failed to connect to 127.0.0.1:%d", port);
  usleep(25000); /* 25 ms */

  Client *sc = Server_available(&s);
  CHECK(sc != NULL, "Server_available did not return accepted Client");
  if (!sc) {
    Client_stop(&c);
    Server_stop(&s);
    return;
  }

  /* Client sends to Server */
  Client_write(&c, "ping");
  usleep(25000);

  CHECK(Client_available(sc) == 1, "Server client socket should have data available");
  const char *req = Client_readString(sc);
  CHECK(strcmp(req, "ping") == 0, "Server received '%s', want 'ping'", req);

  /* Server broadcasts to Client */
  Server_write(&s, "pong");
  usleep(25000);

  CHECK(Client_available(&c) == 1, "Client should have reply data available");
  const char *reply = Client_readString(&c);
  CHECK(strcmp(reply, "pong") == 0, "Client received '%s', want 'pong'", reply);

  Client_stop(&c);
  Server_stop(&s);
  CHECK(s.sock == -1, "Server_stop did not close sock");
  CHECK(c.sock == -1, "Client_stop did not close sock");
}

int main(void) {
  printf("Running Data, Network & IO Library unit tests...\n");

  test_buffered_reader_file();
  test_print_writer();
  test_xml_element();
  test_net_server_client();

  printf("Ran %d checks: %d failures\n", checks, failures);
  if (failures > 0) {
    fprintf(stderr, "FAILED (%d errors)\n", failures);
    return 1;
  }
  printf("ALL TESTS PASSED!\n");
  return 0;
}
