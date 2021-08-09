///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Author(s):  Filip Strugar (filip.strugar@intel.com)
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core/vaCoreIncludes.h"
#include "Core/vaUI.h"

#include "vaRendering.h"

#include "vaTriangleMesh.h"
#include "vaTexture.h"
#include "vaRenderMesh.h"
#include "vaRenderMaterial.h"

#include "Rendering/Shaders/vaSharedTypes.h"

// vaRenderMesh and vaRenderMeshManager are a generic render mesh implementation

namespace Vanilla
{
    class vaTexture;
    class vaRenderMesh;
    class vaRenderMaterial;
    class vaAssetPack;
    class vaAssetPackManager;

    // this needs to be converted to a class, along with type names and other stuff (it started as a simple struct)
    struct vaAsset : public vaUIPropertiesItem, public std::enable_shared_from_this<vaAsset>, vaRuntimeID<vaAsset>
    {
    protected:
        friend class vaAssetPack;
        shared_ptr<vaAssetResource>                     m_resource;
        string                                          m_name;                     // warning, never change this except by using Rename
        vaAssetPack &                                   m_parentPack;
        int                                             m_parentPackStorageIndex;   // referring to vaAssetPack::m_assetList

    protected:
        vaAsset( vaAssetPack & pack, const vaAssetType type, const string & name, const shared_ptr<vaAssetResource> & resourceBasePtr );
        virtual ~vaAsset( );

    public:
        void                                            ReplaceAssetResource( const shared_ptr<vaAssetResource> & newResourceBasePtr );

    public:
        const vaAssetType                               Type;
//        const string                                   StoragePath;

        vaAssetPack &                                   GetAssetPack( ) const                   { return m_parentPack; }

        void                                            UIHighlight( );
        void                                            UIOpenProperties( );

        virtual const vaGUID &                          GetResourceObjectUID( )                 { assert( m_resource != nullptr ); return m_resource->UIDObject_GetUID(); }
        
         shared_ptr<vaAssetResource> &                  GetResource( )                          { return m_resource; }

        template<typename ResourceType>
        shared_ptr<ResourceType>                        GetResource( ) const                    { return std::dynamic_pointer_cast<ResourceType>(m_resource); }

        const string &                                  Name( ) const                           { return m_name; }

        bool                                            Rename( const string & newName );
        //bool                                            Delete( );

        void                                            SetDirtyFlag( ) const;

        virtual bool                                    SaveAPACK( vaStream & outStream );
        virtual bool                                    SerializeUnpacked( vaXMLSerializer & serializer, const string & assetFolder );

        // this returns the shared pointer to this object kept by the parent asset pack
        shared_ptr<vaAsset>                             GetSharedPtr( ) const;

        //void                                            ReconnectDependencies( )                { m_resourceBasePtr->ReconnectDependencies(); }

        static string                                   GetTypeNameString( vaAssetType );

    public:
        virtual string                                  UIPropertiesItemGetDisplayName( ) const override { return vaStringTools::Format( "%s: %s", GetTypeNameString(Type).c_str(), m_name.c_str() ); }
        virtual void                                    UIPropertiesItemTick( vaApplicationBase & application, bool openMenu, bool hovered ) override;
        
        // will set 'asset' to null if it got deleted; check don't proceed using it if that is the case
        static void                                     HandleRightClickContextMenuPopup( vaAsset *& asset, bool hasOpenProperties, bool hasFocusInAssetPack );
    };

    // TODO: all these should be removed, there's really no need to have type-specific vaAsset-s, everything specific should be handled by the vaAssetResource 
    struct vaAssetTexture : public vaAsset
    {
    public:
        typedef vaTexture ResourceType;

    private:
        friend class vaAssetPack;
        vaAssetTexture( vaAssetPack & pack, const shared_ptr<ResourceType> & texture, const string & name );
    
    public:
        shared_ptr<vaTexture>                           GetTexture( ) const                                                     { return std::dynamic_pointer_cast<vaTexture>(m_resource); }
        void                                            ReplaceTexture( const shared_ptr<vaTexture> & newTexture );

        static vaAssetTexture *                         CreateAndLoadAPACK( vaAssetPack & pack, const string & name, vaStream & inStream );
        static vaAssetTexture *                         CreateAndLoadUnpacked( vaAssetPack & pack, const string & name, const vaGUID & uid, vaXMLSerializer & serializer, const string & assetFolder );

        static shared_ptr<vaAssetTexture>               SafeCast( const shared_ptr<vaAsset> & asset ) { if( asset == nullptr ) return nullptr; assert( asset->Type == vaAssetType::Texture ); return std::dynamic_pointer_cast<vaAssetTexture, vaAsset>( asset ); }

        static vaAssetType                              GetType( )                                                              { return vaAssetType::Texture; }
    };

    struct vaAssetRenderMesh : public vaAsset
    {
    public:
        typedef vaRenderMesh ResourceType;

    private:
        friend class vaAssetPack;
        vaAssetRenderMesh( vaAssetPack & pack, const shared_ptr<vaRenderMesh> & mesh, const string & name );

    public:
        shared_ptr<vaRenderMesh>                        GetRenderMesh( ) const                                                     { return std::dynamic_pointer_cast<vaRenderMesh>(m_resource); }
        void                                            ReplaceRenderMesh( const shared_ptr<vaRenderMesh> & newRenderMesh );

        static vaAssetRenderMesh *                      CreateAndLoadAPACK( vaAssetPack & pack, const string & name, vaStream & inStream );
        static vaAssetRenderMesh *                      CreateAndLoadUnpacked( vaAssetPack & pack, const string & name, const vaGUID & uid, vaXMLSerializer & serializer, const string & assetFolder );

        static shared_ptr<vaAssetRenderMesh>            SafeCast( const shared_ptr<vaAsset> & asset ) { if( asset == nullptr ) return nullptr; assert( asset->Type == vaAssetType::RenderMesh ); return std::dynamic_pointer_cast<vaAssetRenderMesh, vaAsset>( asset ); }

        static vaAssetType                              GetType( )                                                              { return vaAssetType::RenderMesh; }
    };

    struct vaAssetRenderMaterial : public vaAsset
    {
    public:
        typedef vaRenderMaterial ResourceType;

    private:
        friend class vaAssetPack;
        vaAssetRenderMaterial( vaAssetPack & pack, const shared_ptr<vaRenderMaterial> & material, const string & name );

    public:
        shared_ptr<vaRenderMaterial>                    GetRenderMaterial( ) const { return std::dynamic_pointer_cast<vaRenderMaterial>( m_resource ); }
        void                                            ReplaceRenderMaterial( const shared_ptr<vaRenderMaterial> & newRenderMaterial );

        static vaAssetRenderMaterial *                  CreateAndLoadAPACK( vaAssetPack & pack, const string & name, vaStream & inStream );
        static vaAssetRenderMaterial *                  CreateAndLoadUnpacked( vaAssetPack & pack, const string & name, const vaGUID & uid, vaXMLSerializer & serializer, const string & assetFolder );

        static shared_ptr<vaAssetRenderMaterial>        SafeCast( const shared_ptr<vaAsset> & asset ) { if( asset == nullptr ) return nullptr; assert( asset->Type == vaAssetType::RenderMaterial ); return std::dynamic_pointer_cast<vaAssetRenderMaterial, vaAsset>( asset ); }

        static vaAssetType                              GetType( )                                                              { return vaAssetType::RenderMaterial; }
    };

    class vaAssetPack : public vaUIPanel
    {
        enum class StorageMode : int32
        {
            Unpacked            = 0,
            APACK               = 1,
            //APACKStreamable,
        };

    protected:
        string                                              m_name;                 // warning - not protected by the mutex and can only be accessed by the main thread
        std::map< string, shared_ptr<vaAsset> >             m_assetMap;
        std::vector< shared_ptr<vaAsset> >                  m_assetList;
        mutable mutex                                       m_assetStorageMutex;

        vaAssetPackManager &                                m_assetPackManager;

        StorageMode                                         m_storageMode           = StorageMode::Unpacked;
        shared_ptr<bool>                                    m_dirty                 = std::make_shared<bool>(false);

        StorageMode                                         m_lastLoadedStorageMode = StorageMode::Unpacked;
        string                                              m_lastLoadedStorage     = "";

        vaFileStream                                        m_apackStorage;
        mutex                                               m_apackStorageMutex;

        shared_ptr<vaBackgroundTaskManager::Task>           m_ioTask;

        std::vector<string>                                 m_assetTypes;

        weak_ptr< vaAsset >                                 m_uiRightClickContextMenuAsset;

        weak_ptr< vaAsset >                                 m_uiHighlight;
        float                                               m_uiHighlightRemainingTime  = 0.0f;

        static string                                       m_uiNameFilter                      ;
        static bool                                         m_uiShowMeshes                      ;
        static bool                                         m_uiShowMaterials                   ;
        static bool                                         m_uiShowTextures                    ;

        static string                                       m_ui_teximport_assetName            ;
        static string                                       m_ui_teximport_textureFilePath      ;
        static vaTextureLoadFlags                           m_ui_teximport_textureLoadFlags     ;
        static vaTextureContentsType                        m_ui_teximport_textureContentsType  ;
        static bool                                         m_ui_teximport_generateMIPs         ;
        static shared_ptr<string>                           m_ui_teximport_lastImportedInfo     ;

        static string                                       m_uiImportAssetFolder;
        static string                                       m_uiImportAssetName;

        //static vaAssetType                                  m_uiCreateEmptyAssetType;
        //static string                                       m_uiCreateEmptyAssetName;

    private:
        friend class vaAssetPackManager;
        explicit vaAssetPack( vaAssetPackManager & assetPackManager, const string & name );
        vaAssetPack( const vaAssetPack & copy ) = delete;
    
    public:
        virtual ~vaAssetPack( );

    public:
        auto &                                              Mutex( ) const                  { return m_assetStorageMutex; }

        string                                              FindSuitableAssetName( const string & nameSuggestion, bool lockMutex );

        void                                                HighlightInUI( const shared_ptr<vaAsset> & asset, bool lockMutex = true );
        shared_ptr<vaAsset>                                 Find( const string & _name, bool lockMutex = true );
        std::vector<shared_ptr<vaAsset>>                    Find( std::function<bool( vaAsset& )> filter, bool lockMutex = true );
        shared_ptr<vaAsset>                                 Find( uint64 runtimeID, bool lockMutex = true );    // search by vaAsset's vaRuntimeID

        //shared_ptr<vaAsset>                                 FindByStoragePath( const string & _storagePath );
        void                                                Remove( const shared_ptr<vaAsset> & asset, bool lockMutex );
        void                                                Remove( vaAsset * asset, bool lockMutex );
        void                                                RemoveAll( bool lockMutex );
        
        vaRenderDevice &                                    GetRenderDevice( );

        bool                                                IsDirty( ) const                            { return *m_dirty; }
        void                                                SetDirty( )                                 { *m_dirty = true; }

    public:
        shared_ptr<vaAssetTexture>                          Add( const shared_ptr<vaTexture> & texture, const string & name, bool lockMutex );
        shared_ptr<vaAssetRenderMesh>                       Add( const shared_ptr<vaRenderMesh> & mesh, const string & name, bool lockMutex );
        shared_ptr<vaAssetRenderMaterial>                   Add( const shared_ptr<vaRenderMaterial> & material, const string & name, bool lockMutex );

        string                                              GetName( ) const                            { assert(vaThreading::IsMainThread()); return m_name; }

        bool                                                RenameAsset( vaAsset & asset, const string & newName, bool lockMutex );
        bool                                                DeleteAsset( vaAsset & asset, bool lockMutex );

        size_t                                              Count( bool lockMutex ) const               { std::unique_lock<mutex> assetStorageMutexLock(m_assetStorageMutex, std::defer_lock ); if( lockMutex ) assetStorageMutexLock.lock(); else m_assetStorageMutex.assert_locked_by_caller(); assert( m_assetList.size() == m_assetMap.size() ); return m_assetList.size(); }

        shared_ptr<vaAsset>                                 At( size_t index, bool lockMutex )          { std::unique_lock<mutex> assetStorageMutexLock(m_assetStorageMutex, std::defer_lock ); if( lockMutex ) assetStorageMutexLock.lock(); else m_assetStorageMutex.assert_locked_by_caller(); if( index >= m_assetList.size() ) return nullptr; else return m_assetList[index]; }

        const shared_ptr<vaBackgroundTaskManager::Task> &   GetCurrentIOTask( )                         { return m_ioTask; }
        void                                                WaitUntilIOTaskFinished( bool breakIfSafe = false );
        bool                                                IsBackgroundTaskActive( ) const;

        // this returns the shared pointer to this object kept by the parent asset manager
        shared_ptr<vaAssetPack>                             GetSharedPtr( ) const;


    protected:
        // these are leftovers - need to be removed 
        // virtual string                                      IHO_GetInstanceName( ) const                { return vaStringTools::Format("Asset Pack '%s'", Name().c_str() ); }
        //virtual void                                        IHO_Draw( );
        virtual bool                                        UIPanelIsDirty( ) const override            { return IsDirty(); }
        virtual string                                      UIPanelGetDisplayName( ) const override     { return m_name; }
        virtual void                                        UIPanelTick( vaApplicationBase & application ) override;

    private:
        void                                                InsertAndTrackMe( shared_ptr<vaAsset> newAsset, bool lockMutex );

        bool                                                LoadAPACKInner( vaStream & inStream, std::vector< shared_ptr<vaAsset> > & loadedAssets, vaBackgroundTaskManager::TaskContext & taskContext );

        void                                                UpdateStorageLocation( const string & newStorage, StorageMode newStorageMode, bool removePrevious );

    friend class vaAssetPackManager;
        // save current contents
        bool                                                SaveAPACK( const string & fileName, bool lockMutex );
        // load contents (current contents are not deleted)
        bool                                                LoadAPACK( const string & fileName, bool async, bool lockMutex );

        // save current contents as XML & folder structure
        bool                                                SaveUnpacked( const string & folderRoot, bool lockMutex );
        // load contents as XML & folder structure (current contents are not deleted)
        bool                                                LoadUnpacked( const string & folderRoot, bool lockMutex );

        bool                                                SingleUnpackedAssetLoad( const string & assetFolder, const string & newName = "", const vaGUID & newUID = vaGUID::Null );

        void                                                SingleTextureImport( string _filePath, string assetName, vaTextureLoadFlags textureLoadFlags, vaTextureContentsType textureContentsType, bool generateMIPs, shared_ptr<string> & outImportedInfo );
    };

    class vaAssetImporter;

    class vaAssetPackManager// : public vaUIPanel
    {
        // contains some standard meshes, etc.
        weak_ptr<vaAssetPack>                               m_defaultPack;

        vaRenderDevice &                                    m_renderDevice;

    protected:
        std::vector<shared_ptr<vaAssetPack>>                m_assetPacks;           // tracks all vaAssetPacks that belong to this vaAssetPackManager
        int                                                 m_UIAssetPackIndex      = 0;

        int                                                 m_hadAsyncOpLastFrame   = 0;
         shared_ptr<int>                                    m_aliveToken      = std::make_shared<int>(42);

    public:
                                                            vaAssetPackManager( vaRenderDevice & renderDevice );
                                                            ~vaAssetPackManager( );

    public:
        shared_ptr<vaAssetPack>                             GetDefaultPack( )                                       { return m_defaultPack.lock(); }
        
        // wildcard '*' or name supported (name with wildcard not yet supported but feel free to add!)
        shared_ptr<vaAssetPack>                             CreatePack( const string & assetPackName );
        void                                                LoadPacks( const string & nameOrWildcard, bool allowAsync = false );
        shared_ptr<vaAssetPack>                             FindLoadedPack( const string & assetPackName );
        shared_ptr<vaAssetPack>                             FindOrLoadPack( const string & assetPackName, bool allowAsync = true );
        void                                                UnloadPack( shared_ptr<vaAssetPack> & pack );
        void                                                UnloadAllPacks( );

        const std::vector<shared_ptr<vaAssetPack>> &        GetAllAssetPacks() const                                { return m_assetPacks; }

        shared_ptr<vaAsset>                                 FindAsset( const string & name );
        std::vector<shared_ptr<vaAsset>>                    FindAssets( std::function<bool( vaAsset & )> filter );
        shared_ptr<vaAsset>                                 FindAsset( const uint64 runtimeID );    // search by vaAsset's vaRuntimeID

        void                                                HighlightInUI( const shared_ptr<vaAsset> & asset );

        shared_ptr<vaRenderMesh>                            FindRenderMesh( const string & name );
        shared_ptr<vaRenderMaterial>                        FindRenderMaterial( const string & name );
        // shared_ptr<vaTexture>                            FindTexture( const string& name );

        bool                                                AnyAsyncOpExecuting( );
        void                                                WaitFinishAsyncOps( );
        bool                                                HadAnyAsyncOpExecutingLastFrame( )                      { return m_hadAsyncOpLastFrame > 0; }

        vaRenderDevice &                                    GetRenderDevice( )                                      { return m_renderDevice; }

        string                                              GetAssetFolderPath( )                                   { return vaCore::GetExecutableDirectoryNarrow() + "Media\\AssetPacks\\"; }

    protected:

        // Many assets have DirectX/etc. resource locks so make sure we're not holding any references 
        friend class vaDirectXCore; // <- these should be reorganized so that this is not called from anything that is API-specific
        void                                                OnRenderingAPIAboutToShutdown( );

    public:
        static shared_ptr<vaAssetResource>                  UIAssetDragAndDropTarget( vaAssetType assetType, const char * label, const vaVector2 & size = vaVector2( 0, 0 ) );

        // returns 'true' if changed
        template< typename AssetType >
        static bool                                         UIAssetLinkWidget( const char * widgetID, vaGUID & assetUID );
    };


    //////////////////////////////////////////////////////////////////////////

    inline shared_ptr<vaAsset> vaAssetPack::Find( const string & _name, bool lockMutex ) 
    { 
        std::unique_lock<mutex> assetStorageMutexLock(m_assetStorageMutex, std::defer_lock );    if( lockMutex ) assetStorageMutexLock.lock(); else m_assetStorageMutex.assert_locked_by_caller();
        auto it = m_assetMap.find( vaStringTools::ToLower( _name ) );                        
        if( it == m_assetMap.end( ) ) 
            return nullptr; 
        else 
            return it->second; 
    }

    // this could get real slow, so maybe use map, same as in m_assetMap
    inline shared_ptr<vaAsset> vaAssetPack::Find( uint64 runtimeID, bool lockMutex ) 
    { 
        std::unique_lock<mutex> assetStorageMutexLock(m_assetStorageMutex, std::defer_lock );    if( lockMutex ) assetStorageMutexLock.lock(); else m_assetStorageMutex.assert_locked_by_caller();
        for( auto it = m_assetList.begin(); it != m_assetList.end(); it++ )
            if( (*it)->RuntimeIDGet() == runtimeID )
                return *it;
        return nullptr;
    }

    template< typename AssetType >
    inline bool vaAssetPackManager::UIAssetLinkWidget( const char * widgetID, vaGUID & assetUID )
    {
        VA_GENERIC_RAII_SCOPE( ImGui::PushID(widgetID);, ImGui::PopID(); );

        string resourceName = ( assetUID == vaGUID::Null ) ? ( "None" ) : ( "ID valid but asset not found / loaded yet" );
        string assetTypeName = vaAsset::GetTypeNameString( AssetType::GetType() );

        auto resource = vaUIDObjectRegistrar::Find<AssetType::ResourceType>( assetUID );    // typename AssetType::ResourceType ?
        vaAsset * resourceAsset = ( resource != nullptr ) ? ( resource->GetParentAsset( ) ) : ( nullptr );
        if( resourceAsset != nullptr )
            resourceName = resourceAsset->Name( );

        ImGui::Text( "%s: %s\n", assetTypeName.c_str(), resourceName.c_str( ) );
        if( resourceAsset != nullptr || assetUID != vaGUID::Null )
        {
            switch( ImGuiEx_SameLineSmallButtons( resourceName.c_str( ), { "[unlink]", "[props]" }, { false, resourceAsset == nullptr } ) )
            {
            case( -1 ): break;
            case( 0 ):
            {
                assetUID = vaGUID::Null;
                return true;
            } break;
            case( 1 ):
            {
                if( resourceAsset != nullptr )
                    vaUIManager::GetInstance( ).SelectPropertyItem( resourceAsset->GetSharedPtr( ) );
            } break;
            default: assert( false ); break;
            }
        }
        else
        {
            string title = vaStringTools::Format( "Drop %s asset here to link", assetTypeName.c_str() );
            auto newAsset = std::dynamic_pointer_cast<AssetType::ResourceType>( vaAssetPackManager::UIAssetDragAndDropTarget( AssetType::GetType(), title.c_str(), { -1, 0 } ) );
            if( newAsset != nullptr )
            {
                assetUID = newAsset->UIDObject_GetUID( );
                return true;
            }
        }
        return false;
    }


}

#include "Scene/vaAssetImporter.h"