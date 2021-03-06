// ****************************************************
// 
// kodiaq Data Acquisition Software
// 
// File    : MasterMongodbConnection.cc
// Author  : Daniel Coderre, LHEP, Universitaet Bern
// Date    : 05.03.2014
// 
// Brief   : Facilitates connection between master and
//           mongodb database in order to update general
//           run information documents.
// 
// ****************************************************

#include "MasterMongodbConnection.hh"
#include "mongo/util/net/hostandport.h"
#include <algorithm> 
#include <stdio.h>
#include <ctype.h>

MasterMongodbConnection::MasterMongodbConnection()
{
   fLog=NULL;
   fOptions=NULL;
   fLogDB = NULL;
   fMonitorDB = NULL;
   fRunsDB = NULL;
   fLogDBName=fMonitorDBName=fRunsDBName="run";
   fBufferUser=fBufferPassword="";
   fRunsCollection="runs";
   mongo::client::initialize();
}

MasterMongodbConnection::MasterMongodbConnection(koLogger *Log)
{
   fLog=Log;
   fOptions=NULL;
   fLogDB = NULL;
   fMonitorDB = NULL;
   fRunsDB = NULL;
   fLogDBName=fMonitorDBName=fRunsDBName="run";
   fBufferUser=fBufferPassword="";
   fRunsCollection="runs";
   mongo::client::initialize();
}
MasterMongodbConnection::~MasterMongodbConnection()
{
}

int MasterMongodbConnection::SetDBs(string logdb, string monitordb, 
				    string runsdb, string logname, 
				    string monitorname, string runsname, 
				    string runscollection, string bufferUser, 
				    string bufferPassword){//, string user,
  //				    string password, string dbauth){
  string errmsg="";
  fLogString = mongo::ConnectionString::parse(logdb, errmsg);
  if(!fLogString.isValid()) cout<<"Proceeding without mongodb log "<<errmsg<<endl;

  fMonitorString = mongo::ConnectionString::parse(monitordb, errmsg);  
  if(!fMonitorString.isValid()) cout<<"Proceeding without monitor DB "<<errmsg<<endl;
  fRunsString = mongo::ConnectionString::parse(runsdb, errmsg);
  if(!fRunsString.isValid()) cout<<"Proceeding without runs DB "<<errmsg<<endl;
  if(fRunsString.isValid() && fLogString.isValid() && fMonitorString.isValid())
    cout<<"All mongodb connection strings confirmed valid"<<endl;

  fBufferUser = bufferUser;
  fBufferPassword=bufferPassword;
  fLogDBName=logname;
  fMonitorDBName=monitorname;
  fRunsDBName=runsname;
  fRunsCollection=runscollection;
  return Connect();
}

int MasterMongodbConnection::Connect(){

  // If we didn't set these yet then quit
  //if(fLogString="" || fMonitorString="" || fRunsString="")
  //return -1;

  // Will check success here
  bool logconnected = true, monitorconnected = true, runsconnected = true;

  // If previously defined then close the old connections
  if(fLogDB != NULL)
    delete fLogDB;
  if(fMonitorDB != NULL)
    delete fMonitorDB;
  if(fRunsDB != NULL )
    delete fRunsDB;

  // Connect logdb
  if(fLogString.isValid()){
    try{
      fLogDB = new mongo::DBClientConnection( true );
      string errmess = "";
      fLogDB = fLogString.connect(errmess);
    }
    catch(const mongo::DBException &e){
      delete fLogDB;
      fLogDB = NULL;
      if(fLog!=NULL) 
	fLog->Error("Problem connecting to log mongo. Caught exception " + 
		    string(e.what()));        
      logconnected = false;
    }
  }
  
  // Connect monitor db
  if( fMonitorString.isValid() ){
    try{
      string errmess = "";
      fMonitorDB = fMonitorString.connect(errmess);
    }
    catch( const mongo::DBException &e ){
      delete fMonitorDB;
      fMonitorDB = NULL;
      if(fLog!=NULL) 
	fLog->Error("Problem connecting to monitor mongo. Caught exception " +
		    string(e.what()));
      monitorconnected = false;
    }
  }
  
  // Connect to runs db
  if( fRunsString.isValid()){
    try{
      string errmess = "";
      fRunsDB = fRunsString.connect(errmess);
    }
    catch( const mongo::DBException &e ){
      delete fRunsDB;
      fRunsDB = NULL;
      if(fLog!=NULL) 
	fLog->Error("Problem connecting to runs mongo. Caught exception " +
		    string(e.what()) );
      runsconnected = false;
    }
  }  
  if( logconnected && monitorconnected && runsconnected && 
      fLogString.isValid() && fRunsString.isValid() && fMonitorString.isValid() )
    return 0;
  return -1;
}
 
void MasterMongodbConnection::InsertOnline(string DB, 
					   string collection,
					   mongo::BSONObj bson){
  string coll = collection.substr(collection.find("."), collection.size()); 
  //collection = "run" + coll;

  // Choose proper DB
  mongo::DBClientBase *thedb = NULL;
  if( DB == "monitor"){
    thedb = fMonitorDB;
  }
  else if( DB=="runs"){
    thedb = fRunsDB;
  }
  else if( DB=="log"){
    thedb = fLogDB;
  }
  
  // Try for the insertion. If it fails then the DB connection must be bad.
  // to avoid spamming the log files just bring the DB connection down. 
  // we can add a 'reconnect' command to the master to bring it up again if the 
  // problem is solved.
  if(thedb != NULL){
    try{
      thedb->insert(collection,bson);
    }
    catch(const mongo::DBException &e){

      // Our DB seems to be down. Try once to reconnect
      //if(Connect()==0)
      //InsertOnline(DB, collection, bson);
      

      if( fLog!= NULL ){
	fLog->Error("Failed inserting to DB '" + DB + "'. The DB seems to be down or unreachable. Continuing without that DB. Offending collection: " + collection + ". Error: " + e.what());
      }
      if( DB=="monitor"){
	delete fMonitorDB;
	fMonitorDB = NULL;
      }
      else if(DB=="runs"){
	delete fRunsDB;
	fRunsDB = NULL;
      }
      else if(DB=="log"){
	delete fLogDB;
	fLogDB = NULL;
      }	  	      
    }
  }
  else{
    if(Connect()==0)
      InsertOnline(DB, collection, bson);
  }
}

void* MasterMongodbConnection::CollectionThreadWrapper(void* data){
  collection_thread_packet_t *payload = static_cast<collection_thread_packet_t*>(data);
  
  MasterMongodbConnection *mongocoll = payload->mongoconnection;
  mongo_option_t options = payload->options;
  string collection = payload->collection;
  string detector = payload->detector;
  vector <string> boardList = payload->boardList;

  int counter = 2;
  int readaheadconstant = 10;
  double readaheadfraction = 0.1;
  time_t startTime = koLogger::GetCurrentTime();
  

  while(mongocoll->IsRunning(detector)){

    time_t currentTime = koLogger::GetCurrentTime();
    double tdiff = fabs(difftime(currentTime, startTime));

    //    cout<<tdiff<<" "<<tdiff*(1+readaheadfraction)<<" "<<tdiff*(1+readaheadfraction)  / 21. <<" "<< counter<<" "<< readaheadconstant<<endl;
    if( (tdiff*(1.+readaheadfraction)  / 21.) + readaheadconstant > counter){
      if(options.hosts.size() !=0){
	vector<string> created;
	mongo_option_t mopts = payload->options;
	for(auto const &h1: options.hosts){
	  if(find(created.begin(), created.end(), h1.second)!=created.end())
	    continue;
	  created.push_back(h1.second);
	  mopts.address = h1.second;
	  cout<<"Thread making mongo opts with "<<h1.second<<" "<<collection<<" "<<counter<<endl;
	  mongocoll->MakeMongoCollection(mopts, collection, boardList, counter);
	  
	}
      }
      else
	mongocoll->MakeMongoCollection(options, collection, boardList, counter);
      counter++;
  }
  sleep(1);
  
  }
  //delete &data;

  return (data);
}

bool MasterMongodbConnection::IsRunning(string detector){
  if(m_collectionThreads.find(detector) == m_collectionThreads.end())
    return false;
  return m_collectionThreads[detector].run;

}

int MasterMongodbConnection::MakeMongoCollection(mongo_option_t mongo_opts, 
						 string collection, 
						 vector<string> boardList,
						 int time_cycle){
  
  string connstring = mongo_opts.address;
  mongo::DBClientBase *bufferDB;

  string collection_base = collection.substr(0, 11);
  
  if(time_cycle != -1){
    mongo_opts.collection=mongo_opts.collection+"_"+koHelper::IntToString(time_cycle);
    collection = collection + "_"+koHelper::IntToString(time_cycle);
  }

  if(fBufferUser!="" && fBufferPassword!="")
    connstring = "mongodb://" + fBufferUser + ":" + fBufferPassword + "@" +
      connstring.substr(10, connstring.size()-10);
  string errstring="";
  mongo::ConnectionString cstring =
    mongo::ConnectionString::parse(connstring,  errstring);

  // Check if string is valid                                                      
  if(!cstring.isValid())
    SendLogMessage( "Problem creating index in buffer DB. Invalid address. " +
		    errstring, KOMESS_WARNING );
  
  try     {
    errstring = "";
    bufferDB = cstring.connect(errstring);
  }
  catch(const mongo::DBException &e)    {
    SendLogMessage( "Problem connecting to mongo buffer. Caught exception " +
		    string(e.what()), KOMESS_ERROR );
    return -1;
  }

  if(time_cycle<=0)   {
    cout<<"Inserting to "<<mongo_opts.database<<".status "<<collection_base<<endl;
    bufferDB->insert(mongo_opts.database+".status",
		     BSON("collection"<< collection_base));
  }

  string collectionName = mongo_opts.database + ".";
  //cout<<collection<<endl;
  if(collection == "DEFAULT")
    collectionName += mongo_opts.collection;
  else {
    collectionName += collection;
    mongo_opts.collection = collection;
  }

  // Set all of the database options here                                          
  // To retain backwards compatibility always make sure they exist         
  // Make string for creating indices                                              
  string index_string = mongo_opts.index_string;

  // Maks stupid index object
  mongo::IndexSpec ispec;
  for(int k=0; k<mongo_opts.indices.size(); k++)
    ispec.addKey(mongo_opts.indices[k]);
  ispec.background();

  
  if(mongo_opts.capped_size != 0)
    bufferDB->createCollection(collectionName,
			       mongo_opts.capped_size,
			       true);
  else{
    //bufferDB->createCollection( collectionName );
    cout<<"Creating collection "<<collectionName<<" with no index on ID"<<endl;
    fLog->Message("Creating collection " + collectionName + " with no index on ID");
    bufferDB->createCollectionWithOptions( 
					  collectionName,
					  0, false, 0,
					  mongo::fromjson("{'autoIndexId': false}")
					   );
  }
  
  cout<<"Creating index on "<<mongo_opts.index_string<<endl;
  if(mongo_opts.index_string!="")
    bufferDB->createIndex( collectionName, ispec );
  //	mongo::fromjson( mongo_opts.index_string )
  //	);
  //mongo_opts.shard_string = "{'module':  1, '_id': 'hashed'}";        
  mongo_opts.shard_string = "{'module': 1}";
  if(mongo_opts.sharding){
    bufferDB->createIndex
      ( collectionName,
	mongo::fromjson( mongo_opts.shard_string )
	);
    mongo::BSONObj ret;
    bufferDB->runCommand
          (
           "admin",
           mongo::fromjson( "{ shardCollection: '"+collectionName+"', key: "+
                            mongo_opts.shard_string+" }"),
           ret
           );
    fLog->Message(ret.toString());
    
    /* This will be beautiful. We want to give mongo a little hint on 
       how it should shard.                        
       This seems to be done by using 'split' to define where the chunks should be 
       We want to chunk on module number.                                          
    */

        // Kill the autobalancer                       
    mongo::WriteConcern WC = mongo::WriteConcern::unacknowledged;
    bufferDB->update("config.settings", BSON("_id" << "balancer"),
		     BSON("$set" << BSON("stopped"<<true)), true,
		     false, &WC );
    
    // First get a list of modules                                                 
    vector <int> modules;
    vector<string> shards;
    shards.push_back("shard_0/eb0:27000");
    shards.push_back("shard_1/eb1:27000");
    shards.push_back("shard_2/eb2:27000");
    
    /*    shards.push_back("shard_0/eb0:27000");            
    shards.push_back("shard_1/eb1:27000");  
    shards.push_back("shard_2/eb2:27000");
    shards.push_back("shard_3/eb0:27001");
    shards.push_back("shard_4/eb1:27001");
    shards.push_back("shard_5/eb2:27001");
    shards.push_back("shard_6/eb0:27002");
    shards.push_back("shard_7/eb1:27002");
    shards.push_back("shard_8/eb2:27002");
    shards.push_back("shard_9/eb0:27003");
    shards.push_back("shard_10/eb1:27003");
    shards.push_back("shard_11/eb2:27003");
    shards.push_back("shard_12/eb0:27004");
    shards.push_back("shard_13/eb1:27004");
    shards.push_back("shard_14/eb2:27004");
    shards.push_back("shard_15/eb0:27005");
    */
    bool LOTS_OF_SPLITS=false;
    if(LOTS_OF_SPLITS){ // make option later
      for(unsigned int x=0; x<boardList.size(); x++){        
	string jsonstring = "{ split: '"+collectionName+  
	  "', middle: { 'module': "+boardList[x]+"}}";  

      // Create the chunk
      cout<<"Telling mongo to split on: "<<jsonstring<<endl;                 
      bufferDB->runCommand("admin", mongo::fromjson( jsonstring ),ret);
      fLog->Message(ret.toString());       

      // MIGRATE THE CHUNKS  
      bufferDB->runCommand("admin", 
			   mongo::fromjson("{ moveChunk: '"+
					   collectionName+
					   "', find: { module: " +
					   boardList[x] + "}, to: '"+
					   shards[x%shards.size()]+"'}"), ret);
      fLog->Message(ret.toString());  
      }                 
    }
    else{
      
      // Cast to int and sort the list. Then store the 1/3 and 2/3 values
      vector <string> splitList;
      vector <int> sortedList;
      for(unsigned int x=0;x<boardList.size();x++)
	sortedList.push_back(koHelper::StringToInt(boardList[x]));
      sort(sortedList.begin(), sortedList.end());

      double nDigis = double(sortedList.size());
      double nShards = double(shards.size());
      int n_in_shard = ceil(nDigis/nShards);	
      fLog->Message("Splitting with " + koHelper::IntToString(n_in_shard) + 
		    " digitizers per shard");
      cout<<"Splitting with "<<n_in_shard<<" digitizers per shard."<<endl;
      if(n_in_shard == 0){
	fLog->Error("Bad shard config. N in shard = 0");
	delete bufferDB;
	return -1;
      }

      for(unsigned int x=n_in_shard;x<sortedList.size(); x+=n_in_shard)
	splitList.push_back( koHelper::IntToString(sortedList[x] ) );
      
      cout<<"Splitting"<<endl;
      // Collection creation at split values
      for(unsigned int x=0; x<splitList.size(); x++){
	string jsonstring = "{ split: '"+collectionName+
	  "', middle: { 'module': "+splitList[x]+"}}";
	cout<<"Telling mongo to split on: "<<jsonstring<<endl;
	bufferDB->runCommand
	  (
	   "admin",
	   mongo::fromjson( jsonstring ),
	   ret
	   );
	fLog->Message("Telling mongo to split on: " + jsonstring);
	fLog->Message(ret.toString());    
	
      }
      
      cout<<"Migrating"<<endl;
      // MIGRATING CHUNKS
      for(int x=-1; x<((int)(splitList.size())); x++){
	string splitVal = koHelper::IntToString(sortedList[0]);

	if(x!=-1)
	  splitVal = splitList[x];	
	bufferDB->runCommand("admin", 
			     mongo::fromjson
			     ( "{ moveChunk: '"
			       +collectionName+
			       "', find: { module: "+
			       splitVal+
			       "},"+ "to: '"+
			       shards[(x+1)%shards.size()]+"' }"), ret);
	fLog->Message("Migrate chunk with " + splitVal + " to " + shards[(x+1)%shards.size()] 
		      + " " + ret.toString());

      }
    }
  
    
  }
  
  delete bufferDB;
  return 0;
}

string MasterMongodbConnection::MakeLocationString(string host, string database){
  string mloc = host;
  int loc_index = mloc.size()-1;
  while(mloc[loc_index] != '/'){
    mloc.pop_back();
    loc_index = mloc.size()-1;
    if(loc_index < 10 ) { //mongodb://	       
      mloc = host;
      break;
    }
  }
  mloc += database;
  return mloc;
}

int MasterMongodbConnection::InsertRunDoc(string user, string name, 
					  string comment, 
					  map<string,koOptions*> options_list,
					  string collection)
/*
  At run start create a new run document and put it into the online.runs database.
  The OID of this document is saved as a private member so the document can be 
  updated when the run ends. 
*/
{

  for ( auto iterator: options_list){

    // Join thread if open
    m_collectionThreads[iterator.first].run=false;
    if(m_collectionThreads.find(iterator.first) != m_collectionThreads.end() &&
       m_collectionThreads[iterator.first].open == true){
      pthread_join(m_collectionThreads[iterator.first].thread,NULL);
      m_collectionThreads[iterator.first].open= false;
    }   


    //Create a bson object with the run information
    mongo::BSONObjBuilder builder;
    builder.genOID();
    
    if(fLog!=NULL) fLog->Message("IN RUN DOC");
    
    // Get the run number for TPC runs
    int run_number = 0;
    mongo::BSONObj obj;
    if(iterator.first == "tpc"){
      try{
	obj = fMonitorDB->findOne(fRunsDBName+"."+fRunsCollection,
				  mongo::Query(BSON("detector" << "tpc")).sort("number",-1));
	if(!obj.isEmpty())
	  run_number = obj.getIntField("number")+1;
      }
      catch (...){
	cout<<"Can't query runs DB"<<endl;
	return -1;
      }
    }

    // Top level stuff
    builder.append("name",name);
    builder.append("user",user);
    builder.append("detector", iterator.first);
    builder.append("number", run_number);
    time_t currentTime;
    time(&currentTime);
    builder.appendTimeT("start",currentTime);
    
    string type="None";
    
    //mongo::BSONObjBuilder detlist;
    //vector<string> detectors;  
    
    koOptions *options = iterator.second;    
    // First create the collection on the buffer db 
    // so the event builder can find it. Index by time and module.
    //mongo::DBClientBase *bufferDB;
    if( options->GetInt("write_mode") == 2 ){ // write to mongo
      string errstring;
      
      // Get mongo options object
      mongo_option_t mongo_opts = options->GetMongoOptions();

      // Needs basic connectivity info to proceed
      if( mongo_opts.address == "" || mongo_opts.database == ""){
	fLog->Error("Writing to mongodb required both a database and address");
	return -1;
      }
      vector<string> boardList;
      for(int x=0; x<options->GetBoards(); x++){
	string serial = koHelper::IntToString(options->GetBoard(x).id);
	if(options->GetBoard(x).type!="V1724")
	  continue;
	boardList.push_back(serial);
      }

      // This does all the complex collection creation. 
      // NOT COMPATIBLE with split hosts. 
      if( options->HasField("rotating_collections") && 
	  options->GetInt("rotating_collections") == 1 ){
	for(unsigned int t=0;t<2;t++){
	  cout<<"Create collection loop "<<t<<endl;
	  vector<string> created;
          mongo_option_t mopts = options->GetMongoOptions();
          for(auto const &h1: mongo_opts.hosts){
            if(find(created.begin(), created.end(), h1.second)!=created.end())
              continue;
            created.push_back(h1.second);
            mopts.address = h1.second;
            cout<<"Main program making mongo opts with "<<h1.second<<" "<<collection<<endl;
            if(MakeMongoCollection(mopts, collection, boardList, t)!=0){
              fLog->Error("Couldn't create mongodb collection");
              return -1;
            }
          }
	}
	// start thread
	if(m_collectionThreads.find(iterator.first) == m_collectionThreads.end()){
	  // create obj
	  collection_thread_t threadobj;
	  threadobj.open = false;
	  threadobj.run = false;
	  m_collectionThreads[iterator.first] = threadobj;
	}

	collection_thread_packet_t *payload = new collection_thread_packet_t;
	payload->mongoconnection = this;
	payload->options = mongo_opts;
	payload->detector = iterator.first;
	payload->collection = collection;
	payload->boardList = boardList;
	m_collectionThreads[iterator.first].open = true;
        m_collectionThreads[iterator.first].run = true;
	pthread_create(&m_collectionThreads[iterator.first].thread,
		       NULL,MasterMongodbConnection::CollectionThreadWrapper,
		       static_cast<void*>(payload));

      }
      else{      
	if(mongo_opts.hosts.size()!=0){
	  vector<string> created;
	  mongo_option_t mopts = options->GetMongoOptions();	 
	  for(auto const &h1: mongo_opts.hosts){
	    if(find(created.begin(), created.end(), h1.second)!=created.end())
	      continue;
	    created.push_back(h1.second);
	    mopts.address = h1.second;
	    cout<<"Making mongo opts with "<<h1.second<<" "<<collection<<endl;
	    if(MakeMongoCollection(mopts, collection, boardList, -1)!=0){
	      fLog->Error("Couldn't create mongodb collection");
	      return -1;
	    }
	  }
	}
	else{
	  if(MakeMongoCollection(mongo_opts, collection, boardList, -1)!=0){
	  fLog->Error("Couldn't create mongodb collection");
	  return -1;
	  }
	}
      }

      // Make mongo location string                                                    
      string mloc = "";
      if(mongo_opts.hosts.size()==0)
	mloc = MakeLocationString(mongo_opts.address, mongo_opts.database);
      else if(mongo_opts.hosts.size()==1)
	mloc = MakeLocationString((*mongo_opts.hosts.begin()).second, 
				   mongo_opts.database);
      else{
	for(auto const &h1 : mongo_opts.hosts){
	  mloc += MakeLocationString(h1.second, mongo_opts.database);
	  mloc+=";";
	}
      }

      mongo::BSONArrayBuilder data_sub;
      mongo::BSONObjBuilder data_entry;
      data_entry.append( "type", "untriggered");
      data_entry.append("status", "transferring");
      data_entry.append("host", "reader");
      data_entry.append( "location", mloc);
      data_entry.append("collection", name);//options->GetString("mongo_collection"));
      data_entry.append( "compressed", options->GetInt("compression"));
      data_sub.append(data_entry.obj());
      builder.appendArray( "data", data_sub.arr() );

    }
    
    mongo::BSONObjBuilder reader;    
    reader.append("ini", options->ExportBSON() );
    // Find the VME option corresponding to DPP
    bool DPP=true;
    for(int x=0;x<options->GetVMEOptions();x++){
      if(options->GetVMEOption(x).address == 0x8080 
	 && options->GetVMEOption(x).value&(1 << 24))
	DPP=false;
    }
    reader.append("self_trigger", DPP);
    builder.append("reader",reader.obj());


    // Trigger sub-object
    mongo::BSONObjBuilder trigger_sub; 
    trigger_sub.append( "mode",options->GetString("trigger_mode") );
    trigger_sub.append( "ended", false );
    if(options->GetString("trigger_mode") != "ignore")
      trigger_sub.append( "status", "waiting_to_be_processed" );
    
    builder.append( "trigger", trigger_sub.obj() );
    
    if(iterator.first == "tpc" || type == "")
      type = options->GetString("source_type");
    mongo::BSONObjBuilder source;
    source.append("type", type);
    if(type=="LED"){
      source.append("frequency", options->GetInt("pulser_freq"));
    }
    builder.append("source", source.obj());

    if(comment != ""){
      mongo::BSONArrayBuilder comment_arr;
      mongo::BSONObjBuilder comment_sub;    
      comment_sub.append( "text", comment);
      comment_sub.appendTimeT( "date", currentTime);
      comment_sub.append( "user", user );
      comment_arr.append( comment_sub.obj() );
      builder.appendArray( "comments", comment_arr.arr() );
    }
    if(comment != ""){
      vector<string> tags = GetHashTags(comment);
      if(tags.size()!=0){
	mongo::BSONArrayBuilder tag_arr;
	for(unsigned int tag=0; tag<tags.size(); tag++){
	  mongo::BSONObjBuilder tag_sub;
	  tag_sub.append("name", tags[tag]);
	  tag_sub.append("user", user);
	  tag_sub.appendTimeT("date", currentTime);	  
	  tag_arr.append(tag_sub.obj());
	}
	builder.appendArray("tags", tag_arr.arr());
      }
    }
    
    //insert into collection
    mongo::BSONObj bObj = builder.obj();
    InsertOnline("runs",fRunsDBName+"."+fRunsCollection,bObj);
    
    
    // store OID so you can update the end time
    mongo::BSONElement OIDElement;
    bObj.getObjectID(OIDElement);
    fLastDocOIDs[iterator.first]=OIDElement.__oid();
  }
  return 0;   
}

vector<string> MasterMongodbConnection::GetHashTags(string comment){
  vector<string> tags;
  string openTag="";
  bool open=false;
  for(unsigned int x=0; x<comment.size(); x++){
    if(open){
      if(!isspace(comment[x]))
	openTag.push_back(comment[x]);
      else{
	tags.push_back(openTag);
	openTag="";
	open = false;
      }
    }
    else if(comment[x]=='#')
      open=true; 
  }
  if(open && openTag.size()>0 && !(all_of(openTag.begin(), 
					  openTag.end(), ::isdigit)) )
    tags.push_back(openTag);
  return tags;
}

int MasterMongodbConnection::UpdateEndTime(string detector)
/*
  When a run is ended we update the run document to indicate that we are 
  finished writing.
  
  Fields updated: 
  
  "endtimestamp": UTC time of end of run
  "data_taking_ended": bool to indicate reader is done with the run
 */
{

  // If thread running mark as dead
  if(m_collectionThreads.find(detector) != m_collectionThreads.end())
    m_collectionThreads[detector].run = false;


  if(fRunsDB == NULL )
    return -1;
  
  try  {

    for( auto iterator : fLastDocOIDs ){
      cout<<"Searching for detector "<<detector<<" in "<<iterator.first<<endl;
      if( iterator.first != detector && detector != "all")
	continue;
      cout<<"Matched!"<<endl;
      if( !iterator.second.isSet() ){
	cout<<"No OID set for detector "<<iterator.first<<endl;
	if(fLog!=NULL) 
	  fLog->Error("MasterMongodbConnection::UpdateEndTime - Want to stop run but don't have the _id field of the run info doc");
	continue;
      }
      cout<<"OID is set"<<endl;

      // Get the time in GMT
      time_t nowTime;
      time(&nowTime);
      
      mongo::BSONObj res;
      string onlinesubstr = "runs_new";
      
      mongo::BSONObjBuilder bo;
      
      bo << "findandmodify" << onlinesubstr.c_str() << 
	"query" << BSON("_id" << iterator.second )<<
	"update" << BSON("$set" << BSON("end" <<
					mongo::Date_t(1000*(nowTime))));

      mongo::BSONObj comnd = bo.obj();
      assert(fRunsDB->runCommand("run",comnd,res));

      mongo::BSONObjBuilder bo2;
      bo2 << "findandmodify" << onlinesubstr.c_str() <<
	"query" << BSON("_id" << iterator.second 
			<< "data.0" << BSON ("$exists" << true ) ) <<
	"update" << BSON("$set" << BSON("data.0.status"<<"transferred"));
      mongo::BSONObj comnd2 = bo2.obj();
      assert(fRunsDB->runCommand("run", comnd2, res));

      // Set to a new random oid so we don't update end times twice
      iterator.second.init();
    }
  }
  catch (const mongo::DBException &e) {
    if(fLog!=NULL) fLog->Error("MasterMongodbConnection::UpdateEndTime - Error fetching and updating run info doc with end time stamp.");
    return -1;
  }   
  return 0;
}

void MasterMongodbConnection::SendStopCommand(string user, string message, 
					      string det){
  mongo::BSONObjBuilder obj;

  obj.append("command","Stop");
  obj.append("detector",det);
  obj.append("user",user);
  obj.append("comment",message);
  InsertOnline("monitor", fMonitorDBName+".daq_control",obj.obj());
  return;

}
void MasterMongodbConnection::SendLogMessage(string message, int priority)
/*
  Log messages are saved into the database. 
  An enum indicates the priority of the message.
  For messages flagged with KOMESS_WARNING or 
  KOMESS_ERROR an alert is created requiring
  a user to mark the alert as solved before 
  starting a new run.
*/
{      

  time_t currentTime;
  time(&currentTime);

  int ID=-1;
  // For WARNINGS and ERRORS we put an additional entry into the alert DB
  // users then get an immediate alert
  if((priority==KOMESS_WARNING || priority==KOMESS_ERROR) 
     && fMonitorDB!=NULL && false){
    
    mongo::BSONObj obj;
    try{
      obj = fMonitorDB->findOne("run.alerts",
				mongo::Query().
				sort("idnum",-1));    
    }
    catch( const mongo::DBException &e ){
      cout<<"Failed to send log message. Maybe mongo is down."<<endl;
      fLog->Error("Failed to send log message to mongodb");
      fLog->Error("Missed message: " + message);
      fLog->Error(e.what());
      return;
    }

    if(obj.isEmpty())
      ID=0;
    else
      ID = obj.getIntField("idnum")+1;

    mongo::BSONObjBuilder alert;
    alert.genOID();
    alert.append("idnum",ID);
    alert.append("priority",priority);
    alert.appendTimeT("timestamp",currentTime);
    alert.append("sender","dispatcher");
    alert.append("message",message);
    alert.append("addressed",false);
    InsertOnline("monitor", fMonitorDBName+".alerts",alert.obj());
  }

  stringstream messagestream;
  string savemessage;
  if(priority==KOMESS_WARNING){
    messagestream<<"The dispatcher has issued a warning with ID "<<
      ID<<" and text: "<<message;
    savemessage=messagestream.str();
  }
  else if(priority == KOMESS_ERROR){
    messagestream<<"The dispatcher has issued an error with ID "<<
      ID<<" and text: "<<message;
    savemessage=messagestream.str();
  }
  else savemessage=message;

  // Save into normal log
   mongo::BSONObjBuilder b;
   b.genOID();
   b.append("message",message);
   b.append("priority",priority);
   b.appendTimeT("time",currentTime);
   b.append("sender","dispatcher");
   InsertOnline("log", fLogDBName+".log",b.obj());
  
}

bool MasterMongodbConnection::CheckForAlerts(){

  //string query = "{'$or': [{'priority': " + koHelper::IntToString(KOMESS_WARNING) +
  //"}, {'priority': "+koHelper::IntToString(KOMESS_ERROR)+"}]}";
  string query = "{'priority': " + koHelper::IntToString(KOMESS_ERROR) + "}";
  mongo::BSONObj doc;
  try{
    doc = fMonitorDB->findOne("run.log",
			      mongo::fromjson(query));
  }
  catch( const mongo::DBException &e ){
    fLog->Error("Failed query log db");
    fLog->Error(e.what());
    return false;
  }
  if(doc.isEmpty())
    return false;
  return true;

}

void MasterMongodbConnection::AddRates(koStatusPacket_t *DAQStatus)
/*
  Update the rates in the online db. 
  Idea: combine all rates into one doc to cut down on queries?
  The online.rates collection should be a TTL collection.
*/
{   
  for(unsigned int x = 0; x < DAQStatus->Slaves.size(); x++)  {
      mongo::BSONObjBuilder b;
      time_t currentTime;
      time(&currentTime);

      if(DAQStatus->Slaves[x].name.empty()){
	fLog->Message("Corrupted slave data");
	continue;
      }

      b.appendTimeT("createdAt",currentTime);
      b.append("node",DAQStatus->Slaves[x].name);
      b.append("bltrate",DAQStatus->Slaves[x].Freq);
      b.append("datarate",DAQStatus->Slaves[x].Rate);
      b.append("runmode",DAQStatus->RunMode);
      b.append("nboards",DAQStatus->Slaves[x].nBoards);
      b.append("timeseconds",(int)currentTime);      
      b.append("cpu", (double)DAQStatus->Slaves[x].cpu);
      b.append("ram", (int)DAQStatus->Slaves[x].ram);
      b.append("ramtot", (int)DAQStatus->Slaves[x].ramtot);      
      InsertOnline("monitor", fMonitorDBName+".daq_rates",b.obj());
   }     
}

void MasterMongodbConnection::UpdateDAQStatus(koStatusPacket_t* DAQStatus,
					      string detector)
/*
  Insert DAQ status doc. The online.daqstatus should be a TTL collection.
 */
{
   mongo::BSONObjBuilder b;
   time_t currentTime;
   time(&currentTime);

   b.appendTimeT("createdAt",currentTime);
   b.append("timeseconds",(int)currentTime);
   b.append("detector", detector);
   b.append("mode",DAQStatus->RunMode);
   if(DAQStatus->DAQState==KODAQ_ARMED)
     b.append("state","Armed");
   else if(DAQStatus->DAQState==KODAQ_RUNNING)
     b.append("state","Running");
   else if(DAQStatus->DAQState==KODAQ_IDLE)
     b.append("state","Idle");
   else if(DAQStatus->DAQState==KODAQ_ERROR)
     b.append("state","Error");
   else
     b.append("state","Undefined");
   b.append("network",DAQStatus->NetworkUp);
   b.append("currentRun",DAQStatus->RunInfo.RunNumber);
   b.append("startedBy",DAQStatus->RunInfo.StartedBy);
   
   string datestring = DAQStatus->RunInfo.StartDate;
   if(datestring.size()!=0){
     datestring.pop_back();
     datestring.pop_back();
     datestring.pop_back();
     //datestring+="+01:00";
   }
   b.append("startTime",datestring);
   b.append("numSlaves",(int)DAQStatus->Slaves.size());

   cout<<"Inserting doc with status for "<<detector<<" in mode "<<DAQStatus->RunMode<< " with state "<<DAQStatus->DAQState<<endl;
   
   InsertOnline("monitor", fMonitorDBName+".daq_status",b.obj());
}

bool MasterMongodbConnection::RunExists(string run_name, string detector){
  if(fRunsDB == NULL)
    return false;
  auto_ptr<mongo::DBClientCursor> cursor;
  try{
    cursor = fRunsDB->query("run.runs_new", 
			    MONGO_QUERY("name"<<run_name<<"detector"<<detector));    
  }
  catch( const mongo::DBException &e ){
    if(fLog!=NULL) {
      fLog->Error("MongoDB error checking runs DB");
      fLog->Error(e.what());
    }
    return false;
  }
  if(cursor->more())
    return true;
  return false;

}

int MasterMongodbConnection::CheckForCommand(string &command, string &user, 
					     string &comment, string &detector,
					     bool &override, 
					     map<string, koOptions*> &options,
					     int &expireAfterSeconds)
//string &detector,
//					     koOptions &options)
/*
  DAQ commands can be written to the online.daqcommand db. 
  These usually come from the web interface but they can actually 
  come from anywhere. The only commands recognized are 
  "Start" and "Stop". The run mode (for "Start") and comments 
  (for both) are also pulled from the doc.
 */
{
  if(fMonitorDB == NULL )
    return -1;

  cout<<"Running CheckForCommand!"<<endl;
  // See if something is in the DB
   mongo::BSONObj b;
   try{
     if(fMonitorDB->count("run.daq_control") ==0)
       return -1;

     auto_ptr<mongo::DBClientCursor> cursor = 
       fMonitorDB->query("run.daq_control", mongo::BSONObj());
     if(cursor->more())
       b = cursor->next();
     else
       return -1;
   }
   catch( const mongo::DBException &e ){
     if(fLog!=NULL) {
       fLog->Error("MongoDB error checking command DB");
       fLog->Error(e.what());
     }
     return -1;
   }


   // Strip data from the doc
   command=b.getStringField("command");
   cout<<"The command is: "<<command<<endl;
   string modeTPC = "";
   string modeMV = "";
   expireAfterSeconds = 0;
   if( command == "Start" ){
     modeTPC = b.getStringField("run_mode_tpc");
     modeMV  = b.getStringField("run_mode_mv");
     override =  b.getBoolField("override");
     expireAfterSeconds=b.getIntField("stop_after_minutes")*60;
     cout<<"Expire: "<<expireAfterSeconds<<endl;
   }
   else
     override = false;
   comment = b.getStringField("comment");
   detector = b.getStringField("detector");
   user=b.getStringField("user");

   //cout<<"Got query: "<<b.toString()<<endl;
   cout<<"Run mode TPC: "<<modeTPC<<" "<<b.getStringField("run_mode_tpc")<<endl;
   cout<<"Detector: "<<detector<<endl;
   // Remove any command docs in the DB. We can only do one at a time
   fMonitorDB->remove("run.daq_control", 
		   MONGO_QUERY("command"<<"Start"<<"detector"<<detector));
   fMonitorDB->remove("run.daq_control",
		   MONGO_QUERY("command"<<"Stop"<<"detector"<<detector)); 

   // If start then get the modes
   if(command=="Start"){
     // Get the run modes
     if( detector == "all" ){
       options["tpc"] = new koOptions();
       options["muon_veto"] = new koOptions();
       if(PullRunMode(modeTPC, *(options["tpc"]))!=0 ||     
	  PullRunMode(modeMV, *(options["muon_veto"])) !=0){
	 delete options["tpc"];
	 delete options["muon_veto"];
	 return -1;
       }
     }
     else{
       options[detector] = new koOptions();
       if(detector=="tpc" && PullRunMode(modeTPC, (*options[detector])) !=0){
	 cout<<"Failed to pull tpc run mode "<<modeTPC<<endl;
	 delete options[detector];
	 return -1;	 
       }
       else if(detector=="muon_veto" &&
	       PullRunMode(modeMV, (*options[detector]))!=0){
	 cout<<"Failed to pull mv run mode "<<modeMV<<endl;
	 delete options[detector];
	 return -1;
       }
       stringstream pp;
       options[detector]->ToStream(&pp);
       //cout<<pp.str()<<endl;
     }
   }
     
   return 0;
}

void MasterMongodbConnection::SyncRunQueue(vector<mongo::BSONObj> dqueue){
  
  if(fMonitorDB == NULL )
    return;
  //fMonitorDB->dropCollection(fMonitorDBName+".daq_queue");
  cout<<"Dropping collection "<<fMonitorDBName<<".daq_queue"<<endl;
  fMonitorDB->dropCollection(fMonitorDBName+".daq_queue");

  for(unsigned int x=0;x<dqueue.size();x++){
    try{
      InsertOnline("monitor", fMonitorDBName+".daq_queue", dqueue[x]);
    }
    catch(...){
      return;
    }
  }
}


vector<mongo::BSONObj> MasterMongodbConnection::GetRunQueue(){

  vector<mongo::BSONObj> retVec;
  
  if(fMonitorDB == NULL )
    return retVec;

  mongo::BSONObj b;
  try{
    if(fMonitorDB->count("run.daq_queue") ==0)
      return retVec;

    auto_ptr<mongo::DBClientCursor> cursor =
      fMonitorDB->query("run.daq_queue", mongo::BSONObj());
    while(cursor->more()){
      b = cursor->next().getOwned();
      retVec.push_back(b);
    }
  }
  catch( const mongo::DBException &e ){
    if(fLog!=NULL) {
      fLog->Error("MongoDB error checking command DB");
      fLog->Error(e.what());
    }
    return retVec;
  }
  return retVec;
}


void MasterMongodbConnection::SendRunStartReply(int response, string message)
/*
  When starting a run a plausibility check is performed. The result of this 
  check is sent using this function. The web interface will wait for an 
  entry in this DB and notify the user if something goes wrong.
 */
// replyenum : 11 - ack 12 - action 13 - start 19 - done 18 - err
{
  mongo::BSONObjBuilder reply;
  reply.append("message",message);
  reply.append("replyenum",response);
  InsertOnline("monitor", fMonitorDBName+".dispatcherreply",reply.obj());
}

void MasterMongodbConnection::ClearDispatcherReply()
/* 
Clears the dispatcher reply db. Run when starting a new run
 */
{
  if(fMonitorDB == NULL) return;
  fMonitorDB->dropCollection(fMonitorDBName+".dispatcherreply");
  return;
}

int MasterMongodbConnection::PullRunMode(string name, koOptions &options)
/*
  Get a run mode from the options DB. Return 0 on success (and set the options
  argument). Return -1 on failure.
 */
{
  if(fMonitorDB == NULL) return -1;
  if(fMonitorDB->count(fMonitorDBName+".run_modes") ==0){
    cout<<"No run modes in online db"<<endl;
    return -1;
  }

   //Find doc corresponding to this run mode
   mongo::BSONObjBuilder query; 
   query.append( "name" , name ); 
   mongo::BSONObj res = fMonitorDB->findOne(fMonitorDBName+".run_modes" , query.obj() ); 
   if(res.nFields()==0) {
     cout<<"Empty run mode obj"<<endl;
     fLog->Error("Top level run mode " + name + " not found.");
     return -1; //empty object
   }

   // Now we want to allow nesting from parent nodes. 
   // The rule is we take all fields from the parent node that are NOT overridden
   // in the child node. If the child node has a field also contained in the parent 
   // node then the child node has preference.
   while(1){
     if(!res.hasField("parent"))
       break;
     if(res["parent"].String() == "none")
       break;

     // Look up the parent doc
     cout<<"Looking for parent "<<res["parent"].String()<<endl;
     mongo::BSONObjBuilder parent_query;
     parent_query.append( "name", res["parent"].String() );
     mongo::BSONObj parent = fMonitorDB->findOne(fMonitorDBName+".run_modes", 
						 parent_query.obj() );
     if( parent.nFields() == 0){
       cout<<"Empty parent. I'll allow it."<<endl;
       fLog->Error("Warning, parent node " + res["parent"].String() + " is empty.");
       break;
     }
     cout<<"Found parent"<<endl;

     // The magic part
     mongo::BSONObjBuilder compositeObj;
     
     // If our new object has a parent
     if(parent.hasField("parent")){
       compositeObj.append("parent", parent["parent"].String());
       cout<<"Appending parent "<<parent["parent"].String()<<endl;
     }
     else{
       compositeObj.append("parent", "none");
       cout<<"Appending parent none"<<endl;
     }

     compositeObj.appendElementsUnique(res);
     compositeObj.appendElementsUnique(parent);
     res = compositeObj.obj();
     cout<<"Made composite object, continuing."<<endl;
   }

   options.SetBSON(res);
   return 0;
}
