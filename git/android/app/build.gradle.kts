plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.example.tagmeow"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "com.example.tagmeow"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            optimization {
                enable = false
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
}

dependencies {
    implementation(libs.activity.ktx)
    implementation(libs.appcompat)
    implementation(libs.constraintlayout)
    implementation(libs.material)
    testImplementation(libs.junit)
    // Android platform org.json is a stub in JVM unit tests, so pull the real one in
    testImplementation(libs.org.json)
    androidTestImplementation(libs.espresso.core)
    androidTestImplementation(libs.ext.junit)
}