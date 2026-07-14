// Real OPDS feeds captured from calibre-web 0.6.26 (the engine inside
// Calibre-Web-Automated) on 2026-07-14. Do not hand-edit the captures.
#pragma once

inline const char* kRootFeed = R"XMLFEED(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <icon>/static/favicon.ico</icon>
  <id>urn:uuid:2853dacf-ed79-42f5-8e8a-a7bb3d1ae6a2</id>
  <updated>2026-07-14T21:57:32+00:00</updated>
  <link rel="self" href="/opds" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link rel="start" title="Start" href="/opds"
        type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link rel="search"
      href="/opds/osd"
      type="application/opensearchdescription+xml"/>
  <link type="application/atom+xml" rel="search" title="Search" href="/opds/search/{searchTerms}" />
  <title>Calibre-Web</title>
  <author>
    <name>Calibre-Web</name>
    <uri>https://github.com/janeczku/calibre-web</uri>
  </author>
  <entry>
    <title>Alphabetical Books</title>
    <link href="/opds/books" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/books</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Books sorted alphabetically</content>
  </entry>
  
  <entry>
    <title>Hot Books</title>
    <link href="/opds/hot" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/hot</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Popular publications from this catalog based on Downloads.</content>
  </entry>
    
  
  <entry>
    <title>Top Rated Books</title>
    <link href="/opds/rated" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/rated</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Popular publications from this catalog based on Rating.</content>
  </entry>
  
  
  <entry>
    <title>Recently added Books</title>
    <link href="/opds/new" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/new</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">The latest Books</content>
  </entry>
  
  
  <entry>
    <title>Random Books</title>
    <link href="/opds/discover" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/discover</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Show Random Books</content>
  </entry>
    
  
  <entry>
    <title>Read Books</title>
    <link href="/opds/readbooks" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/readbooks</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Read Books</content>
  </entry>
  <entry>
    <title>Unread Books</title>
    <link href="/opds/unreadbooks" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/unreadbooks</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Unread Books</content>
  </entry>
  
  
  <entry>
    <title>Authors</title>
    <link href="/opds/author" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/author</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Books ordered by Author</content>
  </entry>
  
  
   <entry>
    <title>Publishers</title>
    <link href="/opds/publisher" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/publisher</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Books ordered by publisher</content>
  </entry>
  
  
  <entry>
    <title>Categories</title>
    <link href="/opds/category" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/category</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Books ordered by category</content>
  </entry>
  
  
  <entry>
    <title>Series</title>
    <link href="/opds/series" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/series</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Books ordered by series</content>
  </entry>
  
  
  <entry>
    <title>Languages</title>
    <link href="/opds/language/" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/language/</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Books ordered by Languages</content>
  </entry>
  
  
  <entry>
    <title>Ratings</title>
    <link href="/opds/ratings" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/ratings</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Books ordered by Rating</content>
  </entry>
  
  
  <entry>
    <title>File formats</title>
    <link href="/opds/formats" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/formats</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Books ordered by file formats</content>
  </entry>
 
   
  <entry>
    <title>Shelves</title>
    <link  href="/opds/shelfindex" type="application/atom+xml;profile=opds-catalog"/>
    <id>/opds/shelfindex</id>
    <updated>2026-07-14T21:57:32+00:00</updated>
    <content type="text">Books organized in shelves</content>
  </entry>
  
</feed>)XMLFEED";

inline const char* kNewBooksFeed = R"XMLFEED(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:dc="http://purl.org/dc/terms/" xmlns:dcterms="http://purl.org/dc/terms/">
  <icon>/static/favicon.ico</icon>
  <id>urn:uuid:2853dacf-ed79-42f5-8e8a-a7bb3d1ae6a2</id>
  <updated>2026-07-14T21:55:55+00:00</updated>
  <link rel="self"
        href="/opds/new?"
        type="application/atom+xml;profile=opds-catalog;type=feed;kind=navigation"/>
  <link rel="start"
        href="/opds"
        type="application/atom+xml;profile=opds-catalog;type=feed;kind=navigation"/>
  <link rel="up"
        href="/opds"
        type="application/atom+xml;profile=opds-catalog;type=feed;kind=navigation"/>



    <link rel="search"
      href="/opds/osd"
      type="application/opensearchdescription+xml"/>
  <link type="application/atom+xml" rel="search" title="Search" href="/opds/search/{searchTerms}" />
  <title>Calibre-Web</title>
  <author>
    <name>Calibre-Web</name>
    <uri>https://github.com/janeczku/calibre-web</uri>
  </author>

  
  
  <entry>
    <title>Test Book One</title>
    <id>urn:uuid:386ae0d1-fb15-401b-b37c-6bb95b9bbecb</id>
    <updated>2026-07-14T21:53:50+00:00</updated>
    
      <author>
        <name>Alpha Author</name>
      </author>
    
    
    <published>2026-07-14T21:53:50+00:00</published>
    
    
    <content type="xhtml"><div xmlns="http://www.w3.org/1999/xhtml">
    
    
    

    

    
        <p>test comment</p>
    
    </div></content>
    
    
    <link rel="http://opds-spec.org/acquisition" href="/opds/download/1/epub/"
          length="1307" title="EPUB" mtime="2026-07-14T21:53:50+00:00" type="application/epub+zip"/>
    
  </entry>
  
  <entry>
    <title>Second Sample</title>
    <id>urn:uuid:51a54244-4915-4dfc-93c3-586ae9372977</id>
    <updated>2026-07-14T21:53:50+00:00</updated>
    
      <author>
        <name>Beta Writer</name>
      </author>
    
    
    <published>2026-07-14T21:53:50+00:00</published>
    
    
    <content type="xhtml"><div xmlns="http://www.w3.org/1999/xhtml">
    
    
    

    

    
        <p>test comment</p>
    
    </div></content>
    
    
    <link rel="http://opds-spec.org/acquisition" href="/opds/download/2/epub/"
          length="1306" title="EPUB" mtime="2026-07-14T21:53:50+00:00" type="application/epub+zip"/>
    
  </entry>
  
  
  
  
</feed>)XMLFEED";

inline const char* kAuthorFeed = R"XMLFEED(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:dc="http://purl.org/dc/terms/" xmlns:dcterms="http://purl.org/dc/terms/">
  <icon>/static/favicon.ico</icon>
  <id>urn:uuid:2853dacf-ed79-42f5-8e8a-a7bb3d1ae6a2</id>
  <updated>2026-07-14T21:57:51+00:00</updated>
  <link rel="self"
        href="/opds/author?"
        type="application/atom+xml;profile=opds-catalog;type=feed;kind=navigation"/>
  <link rel="start"
        href="/opds"
        type="application/atom+xml;profile=opds-catalog;type=feed;kind=navigation"/>
  <link rel="up"
        href="/opds"
        type="application/atom+xml;profile=opds-catalog;type=feed;kind=navigation"/>



    <link rel="search"
      href="/opds/osd"
      type="application/opensearchdescription+xml"/>
  <link type="application/atom+xml" rel="search" title="Search" href="/opds/search/{searchTerms}" />
  <title>Calibre-Web</title>
  <author>
    <name>Calibre-Web</name>
    <uri>https://github.com/janeczku/calibre-web</uri>
  </author>

  
  
  
  <entry>
    <title>All</title>
    <id>/opds/author/letter/00</id>
    <link rel="subsection" type="application/atom+xml;profile=opds-catalog" href="/opds/author/letter/00"/>
  </entry>
  
  <entry>
    <title>A</title>
    <id>/opds/author/letter/A</id>
    <link rel="subsection" type="application/atom+xml;profile=opds-catalog" href="/opds/author/letter/A"/>
  </entry>
  
  <entry>
    <title>B</title>
    <id>/opds/author/letter/B</id>
    <link rel="subsection" type="application/atom+xml;profile=opds-catalog" href="/opds/author/letter/B"/>
  </entry>
  
</feed>)XMLFEED";
