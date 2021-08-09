using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.IO;
using System.Diagnostics;
using System.Threading;

// TODO: verbose output switch
// TODO: recursive -r switch should have 1..9 too...
// TODO: do the 'is up to date' check in some different (faster?) way (maybe write data declarations on the top of the file and 
//       definitions below so that in the first pass data doesn't have to be parsed? sounds like a good idea anyway)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Takes any binary file(s), converts them into text using C-like hex format and outputs it (them) into a cpp file
//
// Parameters:
//
//  -if        : input file name and path; wildcards accepted
//  -id        : root directory to search from for files named with '-if' (in case -id is used, -if cannot contain paths, only names & wildcards)
//  -ep        : prefix for the embedded element names
//  -r         : if '-id' is used, search recursively (default is root level only)
//  -if1..9    : same as if, just another set 
//  -id1..9    : same as id, just another set 
//  -ep1..9    : same as ep, just another set 
//  -o         : output file name
//  -c         : clean build (overwrite output)
//  -a         : append to existing output (default is overwrite) - append will still overwrite all the embedded items with the same embedded 
//               element name (duplicates are not allowed)
//
//
// How to use in cpp:
// 
// include the file somewhere (#include "C:\Temp\test.cpp")
//
// Iterate:
//  for( int i = 0; i < BINARY_EMBEDDER_ITEM_COUNT; i++ )
//  {
//     wchar_t * name			= BINARY_EMBEDDER_NAMES[i];
//     unsigned char * data	= BINARY_EMBEDDER_DATAS[i];
//     int64 dataSize         = BINARY_EMBEDDER_SIZES[i];
//     // ...
//  }

namespace BinaryToCppEmbedder
{
    class EmbeddedElement
    {
        public Guid          UID;
        public string        Name              = null;
        public string        Data              = null;
        public Int64         SizeInBytes       = -1;
        public DateTime      LastModifyTime    = DateTime.MinValue;
        public bool          Tagged            = false;
        //public Int32[]       CRC256            = new Int32[8];

        public EmbeddedElement( )
        {

        }

        public EmbeddedElement( Guid uid, string name, string data, Int64 sizeInBytes, DateTime lastModifyTime )
        {
            this.UID = uid;
            this.Name = name;
            this.Data = data;
            this.SizeInBytes = sizeInBytes;
            this.LastModifyTime = lastModifyTime;
            this.Tagged = false;
        }

        public bool IsOk( )
        {
            return ( Name != null ) && ( Data != null ) && ( SizeInBytes >= 0 ) && ( LastModifyTime != DateTime.MinValue );
        }
    }

    class Program
    {
        static protected bool      verbose = false;

        static int Main( string[] args )
        {
            string[] inFile      = new string[10];
            string[] inDirectory = new string[10];
            string outPath = "";
            string[] embeddedPrefix = new string[10];
            for( int z = 0; z < 10; z++ )
            {
                embeddedPrefix[z] = "embedded:\\";
                inDirectory[z] = "";
            }

            bool recursive = false;
            bool append = false;
            bool cleanRebuild = false;

            int i = 0;
            while( i < args.Length )
            {
                args[i] = args[i].Trim( );

                if( ( args[i].Length > 0 ) && ( args[i][0] != '-' ) )
                {
                    i++;
                    continue;
                }

                //////////////////////////////////////////////////////////////////////////
                // arguments without a parameter
                if( args[i] == "-r" )
                {
                    recursive = true;
                    i++;
                    continue;
                }
                if( args[i] == "-a" )
                {
                    append = true;
                    i++;
                    continue;
                }
                if( args[i] == "-v" )
                {
                    verbose = true;
                    i++;
                    continue;
                }
                if( args[i] == "-c" )
                {
                    cleanRebuild = true;
                    i++;
                    continue;
                }
                //////////////////////////////////////////////////////////////////////////

                //////////////////////////////////////////////////////////////////////////
                // arguments with a single parameter

                if( ( args[i + 1].Length > 0 ) && ( args[i + 1][0] == '-' ) )
                {
                    i++;
                    continue;
                }

                if( args[i] == "-if" )
                {
                    inFile[0] = args[i + 1].Trim( );
                }

                if( args[i] == "-id" )
                {
                    inDirectory[0] = args[i + 1].Trim( );
                }

                if( args[i] == "-ep" )
                {
                    embeddedPrefix[0] = args[i + 1].Trim( );
                }

                for( int mz = 0; mz <= 9; mz++ )
                {
                    if( args[i] == ( "-if" + mz.ToString( ) ) )
                    {
                        inFile[mz] = args[i + 1].Trim( );
                    }

                    if( args[i] == ( "-id" + mz.ToString( ) ) )
                    {
                        inDirectory[mz] = args[i + 1].Trim( );
                    }
                    if( args[i] == ( "-ep" + mz.ToString( ) ) )
                    {
                        embeddedPrefix[mz] = args[i + 1].Trim( );
                    }
                }

                if( args[i] == "-o" )
                {
                    outPath = args[i + 1].Trim( );
                }

                i += 2;
                //////////////////////////////////////////////////////////////////////////
            }

            try
            {
                if( verbose )
                {
                    Console.WriteLine( );
                    Console.WriteLine( "BinaryToCppEmbedder v1.03 (c) Filip Strugar 2012" );
                    Console.WriteLine( );
                }

                /*
                if( fileList.Count == 0 )
                {
                   Console.WriteLine( "No files to process found." );
                   return 0;
                }
                 * */

                List<EmbeddedElement> oldData = new List<EmbeddedElement>( );

                string prevFileID = Guid.NewGuid( ).ToString( "N" );
                if( !cleanRebuild )
                {
                    oldData = ReadExistingIfAny( outPath, ref prevFileID );

                    try
                    {
                        Guid g = Guid.Parse( prevFileID );
                    }
                    catch( System.Exception )
                    {
                        // error with loaded GUID? reset to random
                        prevFileID = Guid.NewGuid( ).ToString( "N" );
                    }
                }

                List<string> fileList      = new List<string>();
                List<string> fileListShort = new List<string>();

                for( int mz = 0; mz <= 9; mz++ )
                {
                    if( ( inFile[mz] == null ) )
                        continue;

                    if( verbose )
                    {
                        Console.WriteLine( );
                        Console.WriteLine( "Searching for '" + inFile[mz] + "' file(s) in '" + inDirectory[mz] + "'... " );
                    }
                    FindInputFiles( inFile[mz], inDirectory[mz], embeddedPrefix[mz], fileList, fileListShort, recursive );
                    if( verbose )
                    {
                        Console.WriteLine( " found " + fileList.Count + "." );
                        Console.WriteLine( );
                    }
                }

                bool modificationsDetected = false;

                List<EmbeddedElement> data = Process( fileList, fileListShort, oldData, out modificationsDetected );

                if( !append )
                {
                    int removedCount = 0;
                    for( int j = data.Count - 1; j >= 0; j-- )
                    {
                        if( !data[j].Tagged )
                        {
                            data.RemoveAt( j );
                            removedCount++;
                        }
                    }
                    if( removedCount > 0 )
                    {
                        if( verbose )
                        {
                            Console.WriteLine( );
                            Console.WriteLine( "Not in append mode, removed " + removedCount.ToString( ) + " existing entries." );
                            Console.WriteLine( );
                        }
                        modificationsDetected = true;
                    }
                }

                if( !modificationsDetected )
                {
                    if( verbose )
                        Console.WriteLine( "No modifications detected, '" + outPath + "' is up to date." );
                    else
                        Console.WriteLine( "BinaryToCppEmbedder: no modifications detected, '" + outPath + "' is up to date." );
                }
                else
                {
                    WriteOutput( outPath, data, prevFileID );
                }

            }
            catch( System.Exception ex )
            {
                if( verbose )
                    Console.WriteLine( ex.Message );
                else
                    Console.WriteLine( "BinaryToCppEmbedder: " + ex.Message );

                WaitForDebuggerIfNeeded( );

                return -1;
            }

            if( verbose )
                Console.WriteLine( "" );
            //Console.WriteLine("BinaryToCppEmbedder finished.");
            //Console.WriteLine("");

            return 0;
        }

        private static void FindFiles( List<string> outList, string path, string fileWildcard, bool recursive )
        {
            //if( recursive )
            //{
            //   string[] dirs = Directory.GetDirectories( path );
            //   foreach( string dir in dirs )
            //      FindFiles( outList, dir, fileWildcard, recursive );
            //}

            string[] files = Directory.GetFiles( path, fileWildcard, ( recursive ) ? ( SearchOption.AllDirectories ) : ( SearchOption.TopDirectoryOnly ) );

            foreach( string file in files )
                outList.Add( file );
        }

        private static string TrimDuplicatedPathChars( string inStr )
        {
            while( true )
            {
                int a = inStr.Length;
                inStr = inStr.Replace( "\\\\", "\\" );
                if( inStr.Length == a ) break;
            }
            // if first one trimmed, return it - it's a network path!!
            if( inStr.Length > 0 && inStr[0] == '\\' )
                inStr = "\\" + inStr;
            return inStr;
        }

        private static void FindInputFiles( string inFile, string inDirectory, string embeddedPrefix, List<string> outFiles, List<string> outFilesShort, bool recursive )
        {
            inFile = inFile.Replace( '/', '\\' );

            if( inDirectory.Length == 0 )
            {
                if( !File.Exists( inFile ) )
                {
                    throw new Exception( "Unable to open file '" + inFile + "'" );
                }
                else
                {
                    outFiles.Add( inFile );
                    outFilesShort.Add( embeddedPrefix + Path.GetFileName( inFile ) );
                    return;
                }
            }

            inDirectory += "\\";

            inDirectory = inDirectory.Replace( '/', '\\' );

            inDirectory = TrimDuplicatedPathChars( inDirectory ); ;

            if( ( inFile.IndexOf( '\\' ) != -1 ) || ( inFile.IndexOf( '/' ) != -1 ) )
            {
                throw new Exception( "'-if' argument cannot be a path if '-id' argument is specified" );
            }

            List<string> ret = new List<string>( );

            FindFiles( ret, inDirectory, inFile, recursive );

            foreach( string str in ret )
            {
                outFiles.Add( str );
                outFilesShort.Add( embeddedPrefix + str.Replace( inDirectory, "" ) );
            }
        }

        private static List<EmbeddedElement> Process( List<string> files, List<string> filesShort, List<EmbeddedElement> existingElements, out bool modificationsDetected )
        {
            Dictionary< string, EmbeddedElement > elements = new Dictionary<string, EmbeddedElement>( );

            modificationsDetected = false;

            foreach( EmbeddedElement element in existingElements )
            {
                elements[element.Name] = element;
            }

            if( verbose )
                Console.WriteLine( "Processing " + files.Count + " files..." );

            // read and convert to strings
            for( int fileIndex = 0; fileIndex < files.Count; fileIndex++ )
            {
                string fileName = files[fileIndex];
                string embeddedName = filesShort[fileIndex];

                if( verbose )
                    Console.Write( " file '" + fileName + "'... " );

                Stream file = null;

                try
                {
                    EmbeddedElement prevE = null;
                    elements.TryGetValue( embeddedName, out prevE );
                    bool exists = prevE != null;

                    DateTime lastModifyTime = File.GetLastWriteTimeUtc( fileName );
                    try
                    {
                        file = File.Open( fileName, FileMode.Open, FileAccess.Read, FileShare.Read );
                    }
                    catch( System.IO.IOException ex )
                    {
                    	Console.WriteLine( "BinaryToCppEmbedder: error reading file '" + fileName + "' : " + ex.Message + " - retrying..." );

                        for( int i = 0; i < 10; i++ )
                        {
                            Thread.Sleep( 500 );
                            try
                            {
                                file = File.Open( fileName, FileMode.Open, FileAccess.Read, FileShare.Read );
                            }
                            catch { }
                            if( file != null )
                                break;
                        }

                        // last try
                        if( file == null )
                            file = File.Open( fileName, FileMode.Open, FileAccess.Read, FileShare.Read );
                    }

                    if( ( prevE != null ) && ( prevE.LastModifyTime == lastModifyTime ) )
                    {
                        if( file.Length != prevE.SizeInBytes )
                            throw new Exception( "Error - timestamps match but sizes don't!" );

                        if( verbose )
                            Console.WriteLine( "skipped (not modified since last time)." );
                        prevE.Tagged = true;
                    }
                    else
                    {
                        if( file.Length > 32 * 1024 * 1024 )
                            throw new Exception( "File '" + fileName + "' bigger than supported size." );

                        StringBuilder contents = new StringBuilder( (int)( file.Length * 3 ) );

                        int elementCount = 0;
                        while( true )
                        {
                            int b = file.ReadByte( );
                            if( b == -1 ) break;

                            // hex
#if USE_HEX_OUTPUT
                           contents.Append( Convert.ToString( b, 16 ) );
                           contents.Append( "," );
#else
                            contents.Append( Convert.ToString( b, 10 ) );
                            contents.Append( "," );
#endif

                            elementCount++;

                            // // add newline every n elements
                            // if( (elementCount % 220) == 0 )
                            // {
                            //    contents.AppendLine();
                            //    contents.Append( "   " );
                            // }
                        }

                        string elementData = contents.ToString( );

                        // if timestamps mismatch, still check to see if content updated
                        if( ( prevE != null ) && ( prevE.Name == embeddedName ) && ( prevE.SizeInBytes == file.Length ) && ( prevE.Data == elementData ) )
                        {
                            // data unchanged
                            if( verbose )
                                Console.WriteLine( "skipped (timestamps different, but data not modified since last time)." );
                            prevE.Tagged = true;

                            // not sure whether to update LastModifyTime - it will speed up next pass, but will change the file. maybe better not!                      
                            // prevE.LastModifyTime = lastModifyTime; 
                            // modificationsDetected = true;
                        }
                        else
                        {
                            // add or replace
                            elements[embeddedName] = new EmbeddedElement( Guid.NewGuid( ), embeddedName, elementData, file.Length, lastModifyTime );
                            elements[embeddedName].Tagged = true;

                            modificationsDetected = true;
                        }

                        if( verbose )
                        {
                            if( exists )
                                Console.WriteLine( "done (existing overridden)." );
                            else
                                Console.WriteLine( "done." );
                        }
                    }
                }
                catch( Exception ex )
                {
                    if( verbose )
                        Console.WriteLine( "error: " + ex.Message );
                    else
                        Console.WriteLine( "BinaryToCppEmbedder: error with reading file '" + fileName + "' : " + ex.Message );

                    WaitForDebuggerIfNeeded( );

                    throw new Exception( "BinaryToCppEmbedder: previous error will result in files not getting embedded, aborting." );
                }
                finally
                {
                    if( file != null )
                        file.Close( );
                }
            }

            List<EmbeddedElement> outData = new List<EmbeddedElement>( );

            foreach( KeyValuePair<string, EmbeddedElement> kvp in elements )
            {
                // some check needed?
                outData.Add( kvp.Value );
            }

            return outData;
        }

        private static string ConvertToSafeCppString( string input )
        {
            return input.Replace( "\\", "\\\\" );
        }

        private static List<EmbeddedElement> ReadExistingIfAny( string inoutPath, ref string prevFileID )
        {
            List<EmbeddedElement> outData = new List<EmbeddedElement>( );

            if( !File.Exists( inoutPath ) )
            {
                if( verbose )
                    Console.WriteLine( "Output file '" + inoutPath + "' does not exist, will create a new one." );
                return outData;
            }

            if( verbose )
                Console.WriteLine( "Reading contents of the existing output file '" + inoutPath + "'..." );

            FileStream fileStream = null;
            StreamReader reader = null;
            try
            {
                fileStream = File.Open( inoutPath, FileMode.Open, FileAccess.Read, FileShare.Read );
                reader = new StreamReader( fileStream, true );

                int count = 0;
                string namesArrayName = null;
                string datasArrayName = null;
                string sizesArrayName = null;
                string timesArrayName = null;

                string line;

                while( ( line = reader.ReadLine( ) ) != null )
                {
                    if( ( count != 0 ) && ( namesArrayName != null ) && ( datasArrayName != null )
                       && ( sizesArrayName != null ) && ( timesArrayName != null ) )
                        break;
                    if( line.IndexOf( "#define BINARY_EMBEDDER_ITEM_COUNT" ) != -1 )
                    {
                        int lb = line.IndexOf( '(' );
                        int rb = line.IndexOf( ')' );
                        string v = line.Substring( lb + 1, rb - lb - 1 );
                        count = Convert.ToInt32( v );
                        continue;
                    }
                    if( line.IndexOf( "#define BINARY_EMBEDDER_NAMES" ) != -1 )
                    {
                        namesArrayName = line.Substring( "#define BINARY_EMBEDDER_NAMES".Length + 1 );

                        prevFileID = namesArrayName.Substring( "s_BE_Names_".Length );

                        continue;
                    }
                    if( line.IndexOf( "#define BINARY_EMBEDDER_DATAS" ) != -1 )
                    {
                        datasArrayName = line.Substring( "#define BINARY_EMBEDDER_DATAS".Length + 1 );
                        continue;
                    }
                    if( line.IndexOf( "#define BINARY_EMBEDDER_SIZES" ) != -1 )
                    {
                        sizesArrayName = line.Substring( "#define BINARY_EMBEDDER_SIZES".Length + 1 );
                        continue;
                    }
                    if( line.IndexOf( "#define BINARY_EMBEDDER_TIMES" ) != -1 )
                    {
                        timesArrayName = line.Substring( "#define BINARY_EMBEDDER_TIMES".Length + 1 );
                        continue;
                    }
                }

                if( ( count == 0 ) || ( namesArrayName == null ) || ( datasArrayName == null )
                   || ( sizesArrayName == null ) || ( timesArrayName == null ) )
                    return outData;

                for( int i = 0; i < count; i++ )
                    outData.Add( new EmbeddedElement( ) );

                while( ( line = reader.ReadLine( ) ) != null )
                {
                    // remove comments
                    int commentIndex = line.IndexOf( "//" );
                    if( commentIndex != -1 )
                        line = line.Substring( 0, commentIndex );

                    // kill whitespaces
                    line = line.Trim( );

                    // empty
                    if( line.Length == 0 )
                        continue;

                    // multiline bingo!
                    if( line[line.Length - 1] != ';' )
                    {
                        string addLine;
                        while( ( addLine = reader.ReadLine( ) ) != null )
                        {
                            addLine = addLine.Trim( );
                            line += addLine;
                            if( line[line.Length - 1] == ';' )
                                break;
                        }
                    }

                    bool isName = false;
                    bool isData = false;
                    int tokenEndIndex = -1;
                    if( ( ( tokenEndIndex = line.IndexOf( namesArrayName + "_" ) ) != -1 ) && ( line.IndexOf( namesArrayName + "[]" ) == -1 ) )
                    {
                        isName = true;
                    }
                    else if( ( ( tokenEndIndex = line.IndexOf( datasArrayName + "_" ) ) != -1 ) && ( line.IndexOf( datasArrayName + "[]" ) == -1 ) )
                    {
                        isData = true;
                    }
                    Debug.Assert( namesArrayName.Length == datasArrayName.Length, "This algorithm expects same lengths" );
                    if( isName || isData )
                    {
                        tokenEndIndex += namesArrayName.Length;
                        if( isName && isData )
                        {
                            Debug.Assert( false, "wtf?" );
                        }
                        int ln = tokenEndIndex;
                        int rn = line.IndexOf( '[' );
                        if( rn == -1 )
                            continue;
                        string strn = line.Substring( ln + 1, rn - ln - 1 );
                        int index = Convert.ToInt32( strn );

                        int lcb = line.IndexOf( '{' );
                        int rcb = line.IndexOf( '}' );
                        if( lcb == -1 ) lcb = line.IndexOf( "\"" );
                        if( rcb == -1 ) rcb = line.IndexOf( "\"", lcb + 1 );

                        if( ( lcb == -1 ) || ( rcb == -1 ) )
                            continue;
                        string strd = line.Substring( lcb + 1, rcb - lcb - 1 );

                        if( isName )
                        {
                            strd = TrimDuplicatedPathChars( strd );
                            outData[index].Name = strd;
                        }
                        else if( isData )
                        {
                            outData[index].Data = strd;
                        }
                    }
                    else
                    {
                        bool isSizes = false;
                        bool isTimes = false;

                        tokenEndIndex = -1;
                        if( ( tokenEndIndex = line.IndexOf( sizesArrayName + "[]" ) ) != -1 )
                            isSizes = true;
                        else if( ( tokenEndIndex = line.IndexOf( timesArrayName + "[]" ) ) != -1 )
                            isTimes = true;
                        Debug.Assert( sizesArrayName.Length == timesArrayName.Length, "This algorithm expects same lengths" );
                        if( isSizes || isTimes )
                        {
                            int lcb = line.IndexOf( '{' );
                            int rcb = line.IndexOf( '}' );
                            if( ( lcb == -1 ) || ( rcb == -1 ) )
                                throw new Exception( "Error reading sizes/times" );

                            string strElements = line.Substring( lcb+1, rcb-lcb-1 );

                            int elIndex = 0;
                            while( strElements.Length > 0 )
                            {
                                int rsep = strElements.IndexOf(',');
                                if( rsep == -1 ) rsep = strElements.IndexOf( '}' );
                                if( rsep == -1 ) rsep = strElements.Length - 1;
                                string strValueStr = strElements.Substring( 0, rsep );
                                if( strValueStr.Length == 0 )
                                    continue;
                                strElements = strElements.Substring( rsep + 1 );

                                Int64 strValueI64 = Convert.ToInt64( strValueStr, 16 );

                                if( isSizes )
                                {
                                    outData[elIndex].SizeInBytes = strValueI64;
                                }
                                else if( isTimes )
                                {
                                    outData[elIndex].LastModifyTime = DateTime.FromBinary( strValueI64 );
                                }
                                elIndex++;
                            }
                        }
                    }
                }

                foreach( EmbeddedElement e in outData )
                {
                    if( !e.IsOk( ) )
                        throw new Exception( "Unspecified error while loading file" );
                }
            }
            catch( System.Exception ex )
            {
                if( verbose )
                {
                    Console.WriteLine( );
                    Console.WriteLine( " error while trying to read the append file: " + ex.Message );
                    Console.WriteLine( );
                }
                else
                {
                    Console.WriteLine( "BinaryToCppEmbedder: error with reading file '" + inoutPath + "' : " + ex.Message );
                }

                WaitForDebuggerIfNeeded( );

                Console.WriteLine( "BinaryToCppEmbedder: previous error will result in all the files getting re-embedded from scratch." );

                outData.Clear( );
                return outData;
            }
            finally
            {
                if( reader != null ) reader.Close( );
                if( fileStream != null ) fileStream.Close( );
            }
            if( verbose )
                Console.WriteLine( " read " + outData.Count + " existing elements." );

            return outData;
        }

        private static void WriteOutput( string outPath, List<EmbeddedElement> data, string prevFileID )
        {
            if( verbose )
            {
                Console.WriteLine( );
                Console.WriteLine( "Writing to '" + outPath + "' ..." );
            }

            FileStream fileStream = File.Create( outPath );
            StreamWriter writer = new StreamWriter( fileStream, Encoding.Unicode );

            string randomName = prevFileID;

            string namesArrayName = "s_BE_Names_" + randomName;
            string datasArrayName = "s_BE_Datas_" + randomName;
            string sizesArrayName = "s_BE_Sizes_" + randomName;
            string timesArrayName = "s_BE_Times_" + randomName;

            writer.WriteLine( @"//////////////////////////////////////////////////////////////////////////" );
            writer.WriteLine( @"//Automatically generated by the BinaryEmbedder tool" );
            writer.WriteLine( @"//////////////////////////////////////////////////////////////////////////" );
            writer.WriteLine( );
            writer.WriteLine( "#undef BINARY_EMBEDDER_ITEM_COUNT" );
            writer.WriteLine( "#undef BINARY_EMBEDDER_NAMES" );
            writer.WriteLine( "#undef BINARY_EMBEDDER_DATAS" );
            writer.WriteLine( "#undef BINARY_EMBEDDER_SIZES" );
            writer.WriteLine( "#undef BINARY_EMBEDDER_TIMES" );
            writer.WriteLine( "#define BINARY_EMBEDDER_ITEM_COUNT (" + data.Count.ToString( ) + ")" );
            writer.WriteLine( "#define BINARY_EMBEDDER_NAMES " + namesArrayName );
            writer.WriteLine( "#define BINARY_EMBEDDER_DATAS " + datasArrayName );
            writer.WriteLine( "#define BINARY_EMBEDDER_SIZES " + sizesArrayName );
            writer.WriteLine( "#define BINARY_EMBEDDER_TIMES " + timesArrayName );
            writer.WriteLine( );

            writer.WriteLine( "// Elements (names)" );
            for( int i = 0; i < data.Count; i++ )
                writer.WriteLine( "static wchar_t " + namesArrayName + "_" + i.ToString( ) + "[] = L\"" + ConvertToSafeCppString( data[i].Name ) + "\";" );

            writer.WriteLine( );

            writer.WriteLine( "// Elements (data)" );
            for( int i = 0; i < data.Count; i++ )
                writer.WriteLine( "static unsigned char " + datasArrayName + "_" + i.ToString( ) + "[] = {" + data[i].Data + "};" );

            writer.WriteLine( );

            writer.WriteLine( "// Array of element names" );
            writer.WriteLine( "static wchar_t * " + namesArrayName + "[] = {" );
            for( int i = 0; i < data.Count; i++ )
                writer.WriteLine( "                          " + namesArrayName + "_" + i.ToString( ) + "," );
            writer.WriteLine( "                             };" );

            writer.WriteLine( "// Array of element data" );
            writer.WriteLine( "static unsigned char * " + datasArrayName + "[] = {" );
            for( int i = 0; i < data.Count; i++ )
                writer.WriteLine( "                          " + datasArrayName + "_" + i.ToString( ) + "," );
            writer.WriteLine( "                             };" );

            writer.WriteLine( "// Array of element data sizes" );
            writer.WriteLine( "static __int64 " + sizesArrayName + "[] = {" );
            for( int i = 0; i < data.Count; i++ )
                writer.WriteLine( "                          0x" + Convert.ToString( data[i].SizeInBytes, 16 ) + "," );
            writer.WriteLine( "                             };" );

            writer.WriteLine( "// Array of element data timestamps" );
            writer.WriteLine( "static __int64 " + timesArrayName + "[] = {" );
            for( int i = 0; i < data.Count; i++ )
                writer.WriteLine( "                          0x" + Convert.ToString( data[i].LastModifyTime.ToBinary( ), 16 ) + "," );
            writer.WriteLine( "                             };" );

            writer.Close( );
            fileStream.Close( );

            if( verbose )
                Console.WriteLine( " done, written " + data.Count + " elements." );
            else
                Console.WriteLine( "BinaryToCppEmbedder: " + data.Count + " files embedded into '" + outPath + "'." );
        }

        static void WaitForDebuggerIfNeeded()
        {
#if DEBUG
            Console.WriteLine( "Debug version of BinaryToCppEmbedder: waiting for debugger to attach..." );
            Debugger.Launch();
            while( !Debugger.IsAttached )
            {
                Thread.Sleep( 100 );
            }
            Console.WriteLine( "Debugger attached, breaking" );
            Debugger.Break();
#endif
        }
    }
}
