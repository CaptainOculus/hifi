//
//  LocationManager.cpp
//  interface/src/location
//
//  Created by Stojce Slavkovski on 2/7/14.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QMessageBox>

#include "Application.h"
#include "LocationManager.h"
#include <UserActivityLogger.h>


const QString POST_PLACE_CREATE = "/api/v1/places/";

LocationManager& LocationManager::getInstance() {
    static LocationManager sharedInstance;
    return sharedInstance;
}

void LocationManager::namedLocationDataReceived(const QJsonObject& data) {
    if (data.isEmpty()) {
        return;
    }

    if (data.contains("status") && data["status"].toString() == "success") {
        emit creationCompleted(LocationManager::Created);
    } else {
        emit creationCompleted(LocationManager::AlreadyExists);
    }
}

void LocationManager::createNamedLocation(NamedLocation* namedLocation) {
    AccountManager& accountManager = AccountManager::getInstance();
    if (accountManager.isLoggedIn()) {
        JSONCallbackParameters callbackParams;
        callbackParams.jsonCallbackReceiver = this;
        callbackParams.jsonCallbackMethod = "namedLocationDataReceived";
        callbackParams.errorCallbackReceiver = this;
        callbackParams.errorCallbackMethod = "errorDataReceived";

        accountManager.authenticatedRequest(POST_PLACE_CREATE, QNetworkAccessManager::PostOperation,
                                            callbackParams, namedLocation->toJsonString().toUtf8());
    }
}

void LocationManager::goToUrl(const QUrl& url) {
//    if (location.startsWith(CUSTOM_URL_SCHEME + "/")) {
//        QStringList urlParts = location.remove(0, CUSTOM_URL_SCHEME.length()).split('/', QString::SkipEmptyParts);
//        
//        if (urlParts.count() > 1) {
//            // if url has 2 or more parts, the first one is domain name
//            QString domain = urlParts[0];
//            
//            // second part is either a destination coordinate or
//            // a place name
//            QString destination = urlParts[1];
//            
//            // any third part is an avatar orientation.
//            QString orientation = urlParts.count() > 2 ? urlParts[2] : QString();
//            
//            goToDomain(domain);
//            
//            // goto either @user, #place, or x-xx,y-yy,z-zz
//            // style co-ordinate.
//            goTo(destination);
//            
//            if (!orientation.isEmpty()) {
//                // location orientation
//                goToOrientation(orientation);
//            }
//        } else if (urlParts.count() == 1) {
//            QString destination = urlParts[0];
//            
//            // If this starts with # or @, treat it as a user/location, otherwise treat it as a domain
//            if (destination[0] == '#' || destination[0] == '@') {
//                goTo(destination);
//            } else {
//                goToDomain(destination);
//            }
//        }
//        return true;
//    }
//    return false;
}

void LocationManager::goToOrientation(QString orientation) {
    if (orientation.isEmpty()) {
        return;
    }

    QStringList orientationItems = orientation.remove(' ').split(QRegExp("_|,"), QString::SkipEmptyParts);

    const int NUMBER_OF_ORIENTATION_ITEMS = 4;
    const int W_ITEM = 0;
    const int X_ITEM = 1;
    const int Y_ITEM = 2;
    const int Z_ITEM = 3;

    if (orientationItems.size() == NUMBER_OF_ORIENTATION_ITEMS) {

        // replace last occurrence of '_' with decimal point
        replaceLastOccurrence('-', '.', orientationItems[W_ITEM]);
        replaceLastOccurrence('-', '.', orientationItems[X_ITEM]);
        replaceLastOccurrence('-', '.', orientationItems[Y_ITEM]);
        replaceLastOccurrence('-', '.', orientationItems[Z_ITEM]);

        double w = orientationItems[W_ITEM].toDouble();
        double x = orientationItems[X_ITEM].toDouble();
        double y = orientationItems[Y_ITEM].toDouble();
        double z = orientationItems[Z_ITEM].toDouble();

        glm::quat newAvatarOrientation(w, x, y, z);

        MyAvatar* myAvatar = Application::getInstance()->getAvatar();
        glm::quat avatarOrientation = myAvatar->getOrientation();
        if (newAvatarOrientation != avatarOrientation) {
            myAvatar->setOrientation(newAvatarOrientation);
            emit myAvatar->transformChanged();
        }
    }
}

bool LocationManager::goToDestination(QString destination) {

    QStringList coordinateItems = destination.remove(' ').split(QRegExp("_|,"), QString::SkipEmptyParts);

    const int NUMBER_OF_COORDINATE_ITEMS = 3;
    const int X_ITEM = 0;
    const int Y_ITEM = 1;
    const int Z_ITEM = 2;
    if (coordinateItems.size() == NUMBER_OF_COORDINATE_ITEMS) {

        // replace last occurrence of '_' with decimal point
        replaceLastOccurrence('-', '.', coordinateItems[X_ITEM]);
        replaceLastOccurrence('-', '.', coordinateItems[Y_ITEM]);
        replaceLastOccurrence('-', '.', coordinateItems[Z_ITEM]);

        double x = coordinateItems[X_ITEM].toDouble();
        double y = coordinateItems[Y_ITEM].toDouble();
        double z = coordinateItems[Z_ITEM].toDouble();

        glm::vec3 newAvatarPos(x, y, z);

        MyAvatar* myAvatar = Application::getInstance()->getAvatar();
        glm::vec3 avatarPos = myAvatar->getPosition();
        if (newAvatarPos != avatarPos) {
            // send a node kill request, indicating to other clients that they should play the "disappeared" effect
            MyAvatar::sendKillAvatar();

            qDebug("Going To Location: %f, %f, %f...", x, y, z);
            myAvatar->slamPosition(newAvatarPos);
            emit myAvatar->transformChanged();
        }

        return true;
    }

    // no coordinates were parsed
    return false;
}

void LocationManager::handleAddressLookupError(QNetworkReply::NetworkError networkError,
                                               const QString& errorString) {
    QString messageBoxString;
    
    if (networkError == QNetworkReply::ContentNotFoundError) {
        messageBoxString = "That address could not be found.";
    } else {
        messageBoxString = errorString;
    }
    
    QMessageBox::warning(Application::getInstance()->getWindow(), "", messageBoxString);
}

void LocationManager::replaceLastOccurrence(const QChar search, const QChar replace, QString& string) {
    int lastIndex;
    lastIndex = string.lastIndexOf(search);
    if (lastIndex > 0) {
        lastIndex = string.lastIndexOf(search);
        string.replace(lastIndex, 1, replace);
    }
}
